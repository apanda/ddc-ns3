import networkx as nx
import sys
from itertools import combinations
import random
import subprocess
import StringIO
from os import path
def contains_ends(ends, failed_edges, graph):
    shortest_path = nx.shortest_path(graph, ends[0], ends[1])
    return any(map(lambda (e1, e2): (e1 in shortest_path) and (e2 in shortest_path), failed_edges))
if __name__=="__main__":
    random.seed()
    topo = None
    perc = 0.0
    if len(sys.argv) < 5:
        print "Usage: <script> <topology> <packets> <delay> <result_file>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    packets = int(sys.argv[2])
    topofile = open(topo)
    edges = []
    for line in topofile:
        nodes = line.split(' ')
        edges.append((int(nodes[0]), int(nodes[1])))
    G = nx.Graph()
    G.add_edges_from(edges)
    nodes = G.nodes()
    delay = eval(sys.argv[3])
    out = open(path.expanduser(sys.argv[4]),'w') 
    from_log = """ 
365=53
107=53
146=53
84=53
138=53
241=53
143=53
2=53
376=53
91=53
3=53
308=53
118=53
433=53
292=53
62=53
208=53
26=53
70=53
266=53
262=53
455=53
114=53
66=53
140=53
445=53
197=53
117=53
239=53
184=53
28=53
61=53
104=53
351=53
171=53
127=53
369=53
323=53
330=53
344=53
444=53
402=53
287=53
201=53
165=53
219=53
172=53
103=53
154=53
250=53
326=53
174=53
353=53
202=53
194=53
276=53
192=53
156=53
223=53
401=53
142=53
297=53
65=53
79=53
389=53
110=53
    """
    source_dest_pairs = ','.join(from_log.strip().split())
    failed_edges = "7=129" 
    current_delay = delay[0]
    while current_delay < delay[1]:
        out.write(str.format("delay = {0}\n", current_delay))
        out.flush()
        executable = str.format("""examples/apanda/traffic-sim --links="{0}" --paths="{1}" --delay="{4}" --topology={2} --packets={3}""",
                                failed_edges, source_dest_pairs, topo, packets, current_delay)
        print executable
        current_delay += delay[2]
        subprocess.call(["./waf", "--run", executable],stdout=out)
