"""Regressions for lexical style/include scanning (no GL/native dependency)."""
import unittest
import tempfile
import io
import sys
import subprocess
from contextlib import redirect_stderr
import audit_clang_format
from unittest.mock import patch
from audit import check_include_rules
from pathlib import Path
from audit.cpp_lex import mask_cpp
import audit_style

class CppAuditTests(unittest.TestCase):
    def test_literals_comments_and_offsets(self):
        source='// class Bad {\nconst char *s=R"tag({ class Bad { })tag"; /* x_ */\nclass UGood {};'
        masked=mask_cpp(source)
        self.assertEqual(len(source),len(masked))
        self.assertEqual(source.count('\n'),masked.count('\n'))
        self.assertNotIn('Bad',masked)
        self.assertNotIn('x_',masked)
        self.assertIn('class UGood',masked)
    def test_forward_declarations_are_not_definitions(self):
        code=mask_cpp('class Existing; class UOwner;\nclass Wrong final : public UOwner {};\nclass URight {};')
        self.assertEqual(audit_style.CLASS_RE.findall(code),['Wrong'])
    def test_real_naming_violations_remain_visible(self):
        self.assertEqual(audit_style.MEMBER_RE.findall(mask_cpp('int state_; // comment_\nconst char *s="string_";')),['state_'])
    def test_growth_guard_remains_fail_closed(self):
        file=audit_style.ROOT/'TestAuditFixture.cpp'
        source='void Function()\n{\n'+('  Statement();\n'*299)+'}\n'
        self.assertTrue(audit_style.long_function_violations(file,source))
        source='void Function()\n{\n  const char *s="} class Hidden {";\n}\n'
        self.assertEqual(audit_style.long_function_violations(file,source),[])
    def test_missing_base_does_not_silently_pass(self):
        failed=subprocess.CompletedProcess([],128,stdout="",stderr="bad revision")
        with patch.object(audit_clang_format.subprocess,"run",return_value=failed):
            with self.assertRaises(RuntimeError):audit_clang_format.git_changed_files("missing")
    def test_missing_formatter_is_an_error(self):
        with patch.object(audit_clang_format.shutil,"which",return_value=None),patch.object(sys,"argv",["audit_clang_format.py"]),redirect_stderr(io.StringIO()):
            self.assertEqual(audit_clang_format.main(),1)

    def test_include_gate_ignores_comments_but_rejects_real_edges(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);world=root/'src/World';world.mkdir(parents=True)
            source=world/'AuditFixture.cpp'
            source.write_text('// #include "Render/Fake.h"\n',encoding='utf-8')
            with patch.object(check_include_rules,'REPO_ROOT',root),patch.object(check_include_rules,'WORLD_SRC',world):
                self.assertEqual(check_include_rules.scan_world_render_includes(),[])
                source.write_text('#include "Render/Forbidden.h"\n',encoding='utf-8')
                found=check_include_rules.scan_world_render_includes()
                self.assertEqual(len(found),1)
                self.assertEqual(found[0]['include'],'Render/Forbidden.h')

    def test_include_literals_can_be_preserved(self):
        source='#include "Render/Test.h"\n// #include "Render/Fake.h"\n'
        masked=mask_cpp(source,strings=False)
        self.assertIn('"Render/Test.h"',masked)
        self.assertNotIn('Fake',masked)

if __name__=='__main__':unittest.main()
