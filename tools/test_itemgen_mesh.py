"""Geometry contracts for the item redesign; no runtime assets are rewritten."""
import math
import unittest
from itemgen.mesh import bevel_box, extrude, shell, transform, triangulate


class MeshTests(unittest.TestCase):
    def test_extrusion_normals_point_outside(self):
        for face in extrude(((-1,-1),(1,-1),(1,1),(-1,1)),2):
            centre=[sum(p[i] for p in face.points)/len(face.points) for i in range(3)]
            self.assertGreater(sum(a*b for a,b in zip(centre,face.normal())),0)

    def test_shell_has_open_cavity_and_closed_rim(self):
        faces=shell((2,2,2),.2)
        self.assertEqual(len(faces),14)
        self.assertEqual(next(f for f in faces if f.name.endswith('.floor')).normal(),(0,1,0))
        self.assertEqual(next(f for f in faces if f.name.endswith('.bottom')).normal(),(0,-1,0))
        for f in faces:
            if '.rim' in f.name: self.assertEqual(f.normal(),(0,1,0))
        self.assertFalse(any(all(p[1]==2 for p in f.points) and len(f.points)>4 for f in faces))

    def test_bevel_stays_in_envelope_and_triangulates(self):
        faces=bevel_box((2,3,1),.2)
        triangles=triangulate(faces)
        self.assertEqual(len(triangles),28)
        for _,_,points,n in triangles:
            self.assertAlmostEqual(sum(x*x for x in n),1)
            for p in points:
                self.assertTrue(all(abs(v)<=bound for v,bound in zip(p,(1,1.5,.5))))

    def test_pivot_and_rotation(self):
        faces=extrude(((-1,-1),(1,-1),(1,1),(-1,1)),1)
        rotated=transform(faces,rotation=(0,0,90),pivot=(0,0,0),offset=(2,0,0))
        a,b=faces[0].points[0],rotated[0].points[0]
        self.assertAlmostEqual(b[0],2-a[1]); self.assertAlmostEqual(b[1],a[0])
        self.assertEqual(faces,transform(faces))

    def test_bad_geometry_is_rejected(self):
        for outline in [((0,0),(0,1),(1,0)),((0,0),(1,0),(1,0)),((0,0),(1,0),(math.nan,1))]:
            with self.assertRaises(ValueError): extrude(outline,1)
        with self.assertRaises(ValueError): shell((1,1,1),.5)
        with self.assertRaises(ValueError): bevel_box((1,1,1),.6)
        with self.assertRaises(ValueError): transform([],taper=0)


if __name__=='__main__': unittest.main()
