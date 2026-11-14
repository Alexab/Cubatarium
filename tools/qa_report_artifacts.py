"""Reset only an explicitly owned JSON QA report and its generated frames."""
from pathlib import Path
import shutil


def reset_report(report: Path, output_root: Path):
    report=report.absolute();root=output_root.resolve()
    frames=Path(str(report)+'.frames')
    # Resolve before recursive deletion, including potential symlinks/junctions.
    for target in (report,frames):
        if target.resolve()==root or not target.resolve().is_relative_to(root):
            raise ValueError('QA artifact target escapes the explicit output directory')
    if report.suffix!='.json': raise ValueError('Expected an owned JSON QA report')
    report.unlink(missing_ok=True)
    if frames.exists():
        if not frames.is_dir() or frames.is_symlink():
            raise ValueError('QA frames must be a generated directory, not a file or symlink')
        shutil.rmtree(frames)
