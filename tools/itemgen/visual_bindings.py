"""Validate authored visual drivers and convert only their length units."""
import copy
import math

CHANNELS={'bow_charge','bow_loaded','use_phase','fill_units'}
VECTORS={'translation_from','translation_to','rotation_from','rotation_to','scale_from','scale_to'}

def prepare_bindings(rows,nodes,scale,item_id):
    if not isinstance(rows,list): raise ValueError('Visual bindings must be a list')
    if item_id in nodes: raise ValueError('Node name duplicates the model root')
    result=copy.deepcopy(rows); controlled=set(); spans=set()
    def vector(value,key):
        if not isinstance(value,list) or len(value)!=3 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in value):
            raise ValueError(f'Invalid visual binding vector: {key}')
    for row in result:
        if not isinstance(row,dict) or row.get('node') not in nodes: raise ValueError('Binding references unknown node')
        name=row['node']
        if name in controlled: raise ValueError('Node has duplicate visual bindings')
        controlled.add(name)
        if 'span' in row:
            if set(row)-{'node','span'}: raise ValueError('Span cannot also have a transform driver')
            span=row['span']
            if not isinstance(span,dict) or set(span)-{'start_node','end_node','start_point','end_point'}:
                raise ValueError('Invalid span binding')
            if nodes[name].get('parent'): raise ValueError('Span output must be a root child')
            for key in ('start_node','end_node'):
                source=span.get(key)
                if source!=item_id and source not in nodes: raise ValueError('Span references unknown anchor')
            for key in ('start_point','end_point'):
                value=span.get(key,[0,0,0]);vector(value,key);span[key]=[v*scale for v in value]
            spans.add(name)
        else:
            if set(row)-({'node','channel','range','curve','visible_above'}|VECTORS): raise ValueError('Unknown visual binding field')
            if row.get('channel') not in CHANNELS: raise ValueError('Unknown visual input channel')
            limits=row.get('range',[0,1])
            if not isinstance(limits,list) or len(limits)!=2 or any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) or abs(v)>1e6 for v in limits) or limits[1]<=limits[0]:
                raise ValueError('Invalid visual input range')
            if row.get('curve','linear') not in ('linear','pulse'): raise ValueError('Unknown visual input curve')
            threshold=row.get('visible_above',-1)
            if isinstance(threshold,bool) or not isinstance(threshold,(int,float)) or not math.isfinite(threshold): raise ValueError('Invalid visibility threshold')
            for key in VECTORS:
                if key not in row: continue
                vector(row[key],key)
                if key.startswith('scale_') and min(row[key])<=.001: raise ValueError('Visual scale must stay positive')
                if key.startswith('translation_'): row[key]=[v*scale for v in row[key]]
    for row in result:
        if 'span' not in row: continue
        for key in ('start_node','end_node'):
            source=row['span'][key];seen=set()
            while source and source!=item_id:
                if source not in nodes: raise ValueError('Span anchor has an unknown parent')
                if source in spans: raise ValueError('Span links depend on a span output')
                if source in seen: raise ValueError('Cyclic anchor hierarchy')
                seen.add(source);source=nodes[source].get('parent','')
    return result
