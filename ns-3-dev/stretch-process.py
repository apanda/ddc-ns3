import networkx as nx
import sys
from itertools import combinations
import random
import subprocess
import StringIO
from os import path
if __name__=="__main__":
    if sys.argv < 2:
        print "Usage: <script> <topology> <results>"
        sys.exit(1)
    topo = path.expanduser(sys.argv[1])
    results = path.expanduser(sys.argv[2])
    topofile = open(topo)
    edges = []
    for line in topofile:
        nodes = line.split(' ')
        edges.append((int(nodes[0]), int(nodes[1])))
    G = nx.Graph()
    G.add_edges_from(edges)
    resultfile = open(results)
    G2 = None
    distances = None
    failure_len = None
    for line in resultfile:
        if line.startswith("failed"):
            parts = line.split(" = ")
            edges_to_remove = eval(parts[1])
            G2 = G.copy()
            G2.remove_edges_from(edges_to_remove)
            distances = nx.all_pairs_dijkstra_path_length(G2)
            failure_len = len(edges_to_remove)
        else:
            parts = line.split(",")
            ttls = parts[2:]
            nodes = map(int, parts[:2])
            distance = distances[nodes[0]][nodes[1]]
            ttls = map(lambda ttl: 256 - int(ttl) if ttl != 'D' else 0, ttls)
            stretch = map(lambda ttl: float(ttl)/float(distance), ttls)
            print str.format("{0},{1}", failure_len, ','.join(map(str, stretch)))

