import unittest
from itemgen.mesh import Face, bevel_box
from itemgen.surface_uv import pack


class SurfaceUvTests(unittest.TestCase):
    def test_authored_grain_axis_follows_world_direction(self):
        face=Face(((-1,-2,0),(1,-2,0),(1,2,0),(-1,2,0)),role='wood')
        island=pack([face],size=256,texels_per_metre=25,directions={'wood':[0,1,0]})[0]
        # Movement along Y follows U; X follows V. Neither axis is stretched.
        self.assertAlmostEqual(island.uv[2][0]-island.uv[1][0],100/256)
        self.assertAlmostEqual(island.uv[1][1]-island.uv[0][1],-50/256)
        for direction in ([0,0,0],[1,float('nan'),0],[0,1],None,[0,True,0]):
            with self.assertRaises(ValueError): pack([face],size=256,texels_per_metre=25,directions={'wood':direction})

    def test_repeatable_nonoverlapping_islands(self):
        faces=bevel_box((.3,.6,.06),.02)
        a=pack(faces,size=256,texels_per_metre=80)
        self.assertEqual(a,pack(list(reversed(faces)),size=256,texels_per_metre=80))
        for i,island in enumerate(a):
            for uv in island.uv: self.assertTrue(all(0<x<1 for x in uv))
            x,y,w,h=island.rect
            for u,v in island.uv:
                self.assertTrue(x<=u*256<x+w and y<=v*256<y+h)
            for other in a[i+1:]:
                ox,oy,ow,oh=other.rect
                self.assertTrue(x+w<=ox or ox+ow<=x or y+h<=oy or oy+oh<=y)

    def test_overflow_is_not_silently_rescaled(self):
        with self.assertRaisesRegex(ValueError,'overflow'):
            pack(bevel_box((2,3,1),.1),size=128,texels_per_metre=100)

    def test_duplicate_names_are_rejected(self):
        faces=bevel_box((.2,.3,.1),.01)
        with self.assertRaisesRegex(ValueError,'unique'):
            pack(faces+faces,size=256,texels_per_metre=10)


if __name__=='__main__': unittest.main()
