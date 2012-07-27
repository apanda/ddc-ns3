import networkx as nx
import sys
from itertools import combinations
import random
import subprocess
import StringIO
from os import path
if __name__=="__main__":
    random.seed()
    topo = None
    perc = 0.0
    if len(sys.argv) < 7:
        print "Usage: <script> <topology> <percentage_of_nodes_to_test> <edges_to_fail> <percent_combination_to_try> <packets> <result_file>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    perc = float(sys.argv[2])
    edges_to_fail = eval(sys.argv[3])
    percent_edges = float(sys.argv[4])
    packets = int(sys.argv[5])
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
    print "Chose nodes"
    out = open(path.expanduser(sys.argv[6]),'w') 
    source_dest_choices = random.sample(list(comb), int(perc * float(len(nodes) * (len(nodes) - 1)) / 2))
    source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
    #for failures in xrange(int(float(len(edges) * edges_to_fail[0])), int(float(len(edges) * edges_to_fail[1]))):
    for failures in xrange(edges_to_fail[0], edges_to_fail[1]):
        tried = 0
        not_connected = 0
        print str.format("Failing {0}", failures)
        fail_edges = combinations(G.edges(), failures)
        for edge in fail_edges:
            if random.random() >= percent_edges:
                continue
            tried = tried + 1
            G2 = G.copy()
            G2.remove_edges_from(edge)
            if not nx.is_connected(G2):
                not_connected = not_connected + 1
                continue
            if tried - not_connected > 5:
                break
            out.write(str.format("failed = {0}\n", edge))
            out.flush()
            #failed_edges =  ','.join(map(lambda e: '='.join(map(lambda k: (k[0], k[1]) if k[0] < k[1] else (k[1], k[0]), map(str, e))), edge))
            failed_edges = map(lambda e: (e[0], e[1]) if e[0] < e[1] else (e[1], e[0]), edge)
            failed_edges = ','.join(map(lambda e:'='.join(map(str, e)) ,failed_edges))
            executable = str.format("""examples/apanda/stretch --links="{0}" --paths="{1}" --topology={2} --packets={3}""",
                                    failed_edges, source_dest_pairs, topo, packets)
            subprocess.call(["./waf", "--run", executable],stdout=out)
        print str.format("not_connected = {0} tried = {1}", not_connected, tried) 
        
