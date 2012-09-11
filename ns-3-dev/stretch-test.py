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
    if len(sys.argv) < 7:
        print "Usage: <script> <topology> <paths_to_test> <edges_to_fail> <# of edges to test> <delay> <packets> <result_file>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    perc = int(sys.argv[2])
    edges_to_fail = eval(sys.argv[3])
    tests = int(sys.argv[4])
    delay = float(sys.argv[5])
    packets = int(sys.argv[6])
    print str.format("{0} {1}", topo, perc)
    topofile = open(topo)
    edges = []
    for line in topofile:
        nodes = line.split(' ')
        edges.append((int(nodes[0]), int(nodes[1])))
    G = nx.Graph()
    G.add_edges_from(edges)
    nodes = G.nodes()
    comb = list(combinations(nodes, 2))
    print "Chose nodes"
    out = open(path.expanduser(sys.argv[7]),'w') 
    #source_dest_choices = random.sample(list(comb), perc)
    #source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
    for failures in xrange(edges_to_fail[0], edges_to_fail[1]):
        tried = 0
        not_connected = 0
        print str.format("Failing {0}", failures)
        fail_edges = list(combinations(G.edges(), failures))
        random.shuffle(fail_edges)
        for edge in fail_edges:
            if tried - not_connected > tests:
                break
            tried = tried + 1
            G2 = G.copy()
            G2.remove_edges_from(edge)
            if not nx.is_connected(G2):
                not_connected = not_connected + 1
                continue
            out.write(str.format("failed = {0}\n", edge))
            out.flush()
            #failed_edges =  ','.join(map(lambda e: '='.join(map(lambda k: (k[0], k[1]) if k[0] < k[1] else (k[1], k[0]), map(str, e))), edge))
            failed_edges = map(lambda e: (e[0], e[1]) if e[0] < e[1] else (e[1], e[0]), edge)
            constrained_pairs = filter(lambda x: contains_ends(x, failed_edges, G), comb)
            if len(constrained_pairs) < int(0.025 * len(comb)):
               not_connected = not_connected + 1
               continue
            source_dest_choices = random.sample(constrained_pairs, min(perc, len(constrained_pairs)))
            source_dest_pairs = ','.join(map(lambda e: '='.join(map(str, e)), source_dest_choices))
            failed_edges = ','.join(map(lambda e:'='.join(map(str, e)) ,failed_edges))
            executable = str.format("""examples/apanda/stretch --links="{0}" --paths="{1}" --topology={2} --packets={3} --delay={4}""",
                                    failed_edges, source_dest_pairs, topo, packets, delay)
            subprocess.call(["./waf", "--run", executable],stdout=out)
        print str.format("not_connected = {0} tried = {1}", not_connected, tried) 
        
