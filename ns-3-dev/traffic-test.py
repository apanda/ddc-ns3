import networkx as nx
import sys
from itertools import combinations, groupby
import random
import subprocess
import StringIO
from os import path
from math import ceil
def contains_ends(ends, failed_edges, graph):
    shortest_path = nx.shortest_path(graph, ends[0], ends[1])
    return any(map(lambda (e1, e2): (e1 in shortest_path) and (e2 in shortest_path), failed_edges))
if __name__=="__main__":
    random.seed()
    topo = None
    perc = 0.0
    if len(sys.argv) < 10:
        print "Usage: <script> <topology>(string) <nodes_to_test>(int) <edges_to_fail>(tuple) <percent_combination_to_try>(meaningless) <packets>(int) <num_tests>(meaningless int) <delay>(three element list) <result_file>(string) <latency>(double)"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    perc = int(sys.argv[2])
    edges_to_fail = eval(sys.argv[3])
    percent_comb = float(sys.argv[4])
    packets = int(sys.argv[5])
    num_tests = int(sys.argv[6])
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
    delay = eval(sys.argv[7])
    out = open(path.expanduser(sys.argv[8]),'w') 
    latency = float(sys.argv[9])
    source_dest_choices = random.sample(list(comb), perc)
    source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
    #for failures in xrange(int(float(len(edges) * edges_to_fail[0])), int(float(len(edges) * edges_to_fail[1]))):
    for trial in xrange(0, num_tests):
        source_dest_choices = random.sample(list(comb), perc)
        source_dest_pairs = ','.join(map(lambda e:'='.join(map(str, e)), source_dest_choices))
        #for failures in xrange(int(float(len(edges) * edges_to_fail[0])), int(float(len(edges) * edges_to_fail[1]))):
        current_delay = delay[0]
        fail_edges = []
        #all_edges = sorted(map(lambda e: (len(filter(lambda x: contains_ends(x, [e], G), comb)), e), G.edges()), key = lambda x:x[0], reverse=True)
        current_delay = delay[0]
        while current_delay < delay[1]:
            out.write(str.format("delay = {0}\n", current_delay))
            out.flush()
            executable = str.format("""examples/apanda/traffic-sim-latency --paths="{0}" --delay="{3}" --topology={1} --packets={2} --latency={4}""",
                                     source_dest_pairs, topo, packets, current_delay, latency)
            print executable
            current_delay += delay[2]
            subprocess.call(["./waf", "--run", executable],stdout=out)
        for failures in xrange(edges_to_fail[0], edges_to_fail[1]):
            tried = 0
            not_connected = 0
            print str.format("Failing {0}", failures)
            edges = G.edges()
            G2 = G.copy()
            G2.remove_edges_from(fail_edges)
            all_edges = sorted(map(lambda e: (len(filter(lambda x: contains_ends(x, [e], G2), comb)), e), G2.edges()), key = lambda x:x[0], reverse=True)
            tried = 0
            not_connected = 0
            print str.format("Failing {0}", failures)
            all_edges_random = []
            for group, vals in groupby(all_edges, lambda x: x[0]):
                vals = list(vals)
                random.shuffle(list(vals))
                all_edges_random.extend(vals)
            fail_edges = []
            for edge in edges:
                G2 = G.copy()
                G2.remove_edges_from([edge])
                if not nx.is_connected(G2):
                    continue
                #out.write(str.format("failed = {0}\n", edge))
                #out.flush()
                #failed_edges =  ','.join(map(lambda e: '='.join(map(lambda k: (k[0], k[1]) if k[0] < k[1] else (k[1], k[0]), map(str, e))), edge))
                failed_edge = map(lambda e: (e[0], e[1]) if e[0] < e[1] else (e[1], e[0]), [edge])
                constrained_pairs = filter(lambda x: contains_ends(x, failed_edge, G), source_dest_choices)
                if len(constrained_pairs) < 2:
                   continue
                fail_edges.append(failed_edge[0])
                if len(fail_edges) >= failures:
                    break
            while True:
                edge = all_edges_random[random.randint(0, min(len(all_edges_random), min(10, int(0.15 * float(len(all_edges_random))))))][1]
                G2 = G.copy()
                G2.remove_edges_from(fail_edges)
                G2.remove_edges_from([edge])
                if not nx.is_connected(G2) or edge in fail_edges:
                    continue
                fail_edges.append(edge)
                if len(fail_edges) >= failures:
                    break
            out.write(str.format("failed = {0}\n", fail_edges))
            out.flush()
            #failed_edges =  ','.join(map(lambda e: '='.join(map(lambda k: (k[0], k[1]) if k[0] < k[1] else (k[1], k[0]), map(str, e))), edge))
            failed_edges = ','.join(map(lambda e:'='.join(map(str, e)) ,fail_edges))
            current_delay = delay[0]
            while current_delay < delay[1]:
                out.write(str.format("delay = {0}\n", current_delay))
                out.flush()
                executable = str.format("""examples/apanda/traffic-sim-latency --links="{0}" --paths="{1}" --delay="{4}" --topology={2} --packets={3} --latency={5}""",
                                        failed_edges, source_dest_pairs, topo, packets, current_delay, latency)
                print executable
                current_delay += delay[2]
                subprocess.call(["./waf", "--run", executable],stdout=out)
            print str.format("not_connected = {0} tried = {1}", not_connected, tried) 
    
