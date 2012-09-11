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
    if len(sys.argv) < 8:
        print "Usage: <script> <topology> <nodes_to_test> <edges_to_fail> <packets> <num_tests> <delay> <result_file>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    perc = int(sys.argv[2])
    edges_to_fail = eval(sys.argv[3])
    packets = int(sys.argv[4])
    num_tests = int(sys.argv[5])
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
    delay = eval(sys.argv[6])
    out = open(path.expanduser(sys.argv[7]),'w') 
    #for failures in xrange(int(float(len(edges) * edges_to_fail[0])), int(float(len(edges) * edges_to_fail[1]))):
    for failures in xrange(edges_to_fail[0], edges_to_fail[1]):
        tried = 0
        not_connected = 0
        print str.format("Failing {0}", failures)
        fail_edges = list(combinations(G.edges(), failures))
        random.shuffle(fail_edges)
        for edge in fail_edges:
            if tried - not_connected > num_tests:
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
            source_dest_choices = random.sample(list(constrained_pairs), min(perc, len(constrained_pairs)))
            source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
            failed_edges = ','.join(map(lambda e:'='.join(map(str, e)) ,failed_edges))
            current_delay = delay[0]
            while current_delay < delay[1]:
                out.write(str.format("delay = {0}\n", current_delay))
                out.flush()
                executable = str.format("""examples/apanda/tcp-burst --links="{0}" --paths="{1}" --delay="{4}" --topology={2} --packets={3}""",
                                        failed_edges, source_dest_pairs, topo, packets, current_delay)
                print executable
                current_delay += delay[2]
                subprocess.call(["./waf", "--run", executable],stdout=out)
        print str.format("not_connected = {0} tried = {1}", not_connected, tried) 
        
