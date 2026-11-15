"""Geometry contracts for the item redesign; no runtime assets are rewritten."""
import math
import unittest
from itemgen.mesh import bevel_box, blade, extrude, shell, ridged_shell, tube, transform, triangulate, revolve, ring


class MeshTests(unittest.TestCase):
    def test_sleeve_is_open_at_both_ends_and_has_closed_walls(self):
        faces=tube((.2,.3,.16),.015)
        self.assertEqual(len(triangulate(faces)),32)
        edges={}
        for face in faces:
            if '.top' in face.name:self.assertEqual(face.normal(),(0,1,0))
            if '.bottom' in face.name:self.assertEqual(face.normal(),(0,-1,0))
            for a,b in zip(face.points,face.points[1:]+face.points[:1]):
                key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
        self.assertTrue(all(value==2 for value in edges.values()))
        self.assertTrue(all(abs(p[0])>=.0849 or abs(p[2])>=.0649 for f in faces for p in f.points))

    def test_ridged_shell_keeps_the_cavity_and_has_no_duplicate_cap(self):
        faces=ridged_shell((.4,.2,.35),.02,.05)
        self.assertFalse(any(f.name.endswith('.bottom') for f in faces))
        self.assertEqual(next(f for f in faces if f.name.endswith('.floor')).normal(),(0,1,0))
        self.assertLess(next(f for f in faces if f.name.endswith('.ridge_left')).normal()[0],0)
        self.assertGreater(next(f for f in faces if f.name.endswith('.ridge_right')).normal()[0],0)
        edges={}
        for face in faces:
            face.normal()
            for a,b in zip(face.points,face.points[1:]+face.points[:1]):
                key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
        self.assertTrue(all(count==2 for count in edges.values()))
        self.assertAlmostEqual(min(p[1] for f in faces for p in f.points),-.05)
        self.assertAlmostEqual(max(p[1] for f in faces for p in f.points),.2)
        for rise in (0,-.1,float('nan'),float('inf')):
            with self.assertRaises(ValueError):ridged_shell((.4,.2,.35),.02,rise)

    def test_sloping_shell_opening_is_closed_and_preserves_wall_thickness(self):
        for builder in (lambda **kw:shell((.4,.2,.35),.02,**kw),lambda **kw:ridged_shell((.4,.2,.35),.02,.05,**kw)):
            faces=builder(front_rim_drop=.07)
            edges={}
            for face in faces:
                face.normal()
                for a,b in zip(face.points,face.points[1:]+face.points[:1]):
                    key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
            self.assertTrue(all(count==2 for count in edges.values()))
            front=next(f for f in faces if f.name.endswith('.outer0'))
            back=next(f for f in faces if f.name.endswith('.outer2'))
            self.assertAlmostEqual(max(p[1] for p in front.points),.13)
            self.assertAlmostEqual(max(p[1] for p in back.points),.2)
            self.assertTrue(all(p[1]==.02 for p in next(f for f in faces if f.name.endswith('.floor')).points))
            self.assertEqual(builder(),builder(front_rim_drop=0))
            for drop in (-.1,.18,float('nan'),True):
                with self.assertRaises(ValueError):builder(front_rim_drop=drop)

    def test_anisotropic_scale_preserves_pivot_and_outward_normals(self):
        faces=bevel_box((.2,.4,.2),.02)
        scaled=transform(faces,scale=(2,.5,1),pivot=(0,0,0))
        points=[p for f in scaled for p in f.points]
        self.assertAlmostEqual(max(p[0] for p in points)-min(p[0] for p in points),.4)
        self.assertAlmostEqual(max(p[1] for p in points)-min(p[1] for p in points),.2)
        for face in scaled:
            centre=[sum(p[i] for p in face.points)/len(face.points) for i in range(3)]
            self.assertGreater(sum(a*b for a,b in zip(centre,face.normal())),0)
        for scale in ((1,0,1),(1,-1,1),(1,float('nan'),1),(1,1)):
            with self.assertRaises(ValueError):transform(faces,scale=scale)

    def test_blade_web_edge_winding_and_closed_surface(self):
        faces=blade(((-1,-2),(1,-2),(1,2),(-1,2)),.2,.02,edge_role='edge')
        edges={}
        for face in faces:
            centre=[sum(p[i] for p in face.points)/len(face.points) for i in range(3)]
            self.assertGreater(sum(a*b for a,b in zip(centre,face.normal())),0)
            for a,b in zip(face.points,face.points[1:]+face.points[:1]):
                key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
        self.assertTrue(all(count==2 for count in edges.values()))
        self.assertTrue(any(0<f.normal()[2]<1 for f in faces))
        self.assertEqual({f.role for f in faces},{'body','edge'})
        for depth,edge,fraction in ((.1,.2,.2),(.1,0,.2),(.1,.01,0),(.1,.01,.5)):
            with self.assertRaises(ValueError): blade(((-1,-1),(1,-1),(0,1)),depth,edge,fraction)

    def test_authored_radial_asymmetry_stays_convex_closed_and_deterministic(self):
        profile=((-1,.5),(0,1),(1,.4))
        scales=[1.04,.95,1.07,1.02,.94,1.06,.98,1]
        faces=revolve(profile,radial_scales=scales)
        self.assertEqual(faces,revolve(profile,radial_scales=scales))
        points=[p for face in faces for p in face.points]
        self.assertNotAlmostEqual(max(p[0] for p in points),-min(p[0] for p in points))
        self.assertNotAlmostEqual(max(p[2] for p in points),-min(p[2] for p in points))
        edges={}
        for face in faces:
            centre=[sum(p[i] for p in face.points)/len(face.points) for i in range(3)]
            self.assertGreater(sum(a*b for a,b in zip(centre,face.normal())),0)
            for a,b in zip(face.points,face.points[1:]+face.points[:1]):
                key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
        self.assertTrue(all(count==2 for count in edges.values()))
        self.assertEqual(revolve(profile),revolve(profile,radial_scales=[1]*8))
        for invalid in ([1]*7,[1]*7+[0],[1]*7+[float('nan')],[1]*7+[True],[1,.1,1,1,1,1,1,1]):
            with self.assertRaises(ValueError):revolve(profile,radial_scales=invalid)

    def test_hollow_ring_retains_its_open_center(self):
        faces=ring(.05,.03,.02)
        self.assertEqual(len(triangulate(faces)),64)
        for face in faces:
            self.assertTrue(all((p[0]*p[0]+p[1]*p[1])**.5>=.03-1e-8 for p in face.points))
            if '.front' in face.name: self.assertAlmostEqual(face.normal()[2],1)
            if '.back' in face.name: self.assertAlmostEqual(face.normal()[2],-1)
    def test_revolved_solid_winding_and_bounds(self):
        faces=revolve(((-.1,.05),(0,.12),(.1,.06)))
        for face in faces:
            centre=[sum(p[i] for p in face.points)/len(face.points) for i in range(3)]
            self.assertGreater(sum(a*b for a,b in zip(centre,face.normal())),0)
        self.assertEqual(len(triangulate(faces)),44)
        with self.assertRaises(ValueError): revolve(((0,.1),(0,.2)))
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
