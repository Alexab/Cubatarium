"""Presentation parameters use independent units; only grip coordinates follow fit."""
import copy
import math


def prepare_presentation(value,scale,centre):
    if not isinstance(value,dict):raise ValueError('Presentation must be an object')
    result=copy.deepcopy(value)
    allowed={'right_grip','left_grip','fp_euler_deg','fp_hand_offset','fp_scale','icon_euler_deg','fp_arm_euler_deg','tp_euler_deg','tp_scale','ground_euler_deg','support_grip_node','fp_aim_euler_deg','tp_aim_euler_deg','carry_euler_deg','carry_right_grip','carry_left_grip'}
    if set(result)-allowed:raise ValueError('Unknown presentation field')
    for key,value in result.items():
        if key=='support_grip_node':
            if not isinstance(value,str) or not value:raise ValueError('Support grip must name a node')
        elif key in ('fp_scale','tp_scale'):
            if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or not .1<=value<=3:raise ValueError('Invalid presentation scale')
        elif not isinstance(value,list) or len(value)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in value):
            raise ValueError(f'Invalid presentation vector: {key}')
    for key in ('right_grip','left_grip','carry_right_grip','carry_left_grip'):
        if key in result:result[key]=[(result[key][i]-centre[i])*scale for i in range(3)]
    return result
