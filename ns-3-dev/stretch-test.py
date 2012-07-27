import networkx as nx
import sys
from itertools import combinations
import random
import subprocess
import StringIO
from os import path
if __name__=="__main__":
    topo = None
    perc = 0.0
    if len(sys.argv) < 6:
        print "Usage: <script> <topology> <percentage_of_nodes_to_test> <edges_to_fail> <packets> <result_file>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    perc = float(sys.argv[2])
    edges_to_fail = int(sys.argv[3])
    packets = int(sys.argv[4])
    print str.format("{0} {1}", topo, perc)
    topofile = open(topo)
    edges = []
    for line in topofile:
        nodes = line.split(' ')
        edges.append((int(nodes[0]), int(nodes[1])))
    G = nx.Graph()
    G.add_edges_from(edges)
    nodes = G.nodes()
    comb = combinations(nodes, 2)
    source_dest_choices = random.sample(list(comb), int(perc * float(len(nodes) * (len(nodes) - 1)) / 2))
    source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
    fail_edges = combinations(G.edges(), edges_to_fail)
    out = open(path.expanduser(sys.argv[5]),'w') 
    for edge in fail_edges:
        G2 = G.copy()
        G2.remove_edges_from(edge)
        if not nx.is_connected(G2):
            continue
        out.write(str.format("failed = {0}\n", edge))
        out.flush()
        #failed_edges =  ','.join(map(lambda e: '='.join(map(lambda k: (k[0], k[1]) if k[0] < k[1] else (k[1], k[0]), map(str, e))), edge))
        failed_edges = map(lambda e: (e[0], e[1]) if e[0] < e[1] else (e[1], e[0]), edge)
        failed_edges = ','.join(map(lambda e:'='.join(map(str, e)) ,failed_edges))
        executable = str.format("""examples/apanda/stretch --links="{0}" --paths="{1}" --topology={2} --packets={3}""",
                                failed_edges, source_dest_pairs, topo, packets)
        subprocess.call(["./waf", "--run", executable],stdout=out)
        
