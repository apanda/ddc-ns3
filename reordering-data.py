import sys
def main(files):
    for fname in files:
        f = open(fname)
        delay = 0.0
        for line in f:
            try:
                if line.strip().startswith("failed"):
                    continue
                elif line.startswith("delay"):
                    delay = float(line.strip().split(" = ")[1])
                else:
                    if 'R' not in line.strip():
                        continue
                    line = line.strip().replace('R', '')
                    splits = line.split(',')[2:]
                    diff = 0
                    next_expected = 0
                    curr_dup_ack = 0
                    max_dup_ack = 0
                    received = {x: False for x in xrange(0, len(splits) + 1)}
                    nacks = {x: 0 for x in xrange(0, len(splits) + 1)}
                    for r in xrange(0, len(splits)):
                        if splits[r] != 'D':
                            seq = int(splits[r])
                            curr_diff = abs(r - seq)
                            if curr_diff > diff:
                                diff = curr_diff
                            assert(not received[seq])
                            received[seq] = True
                            if seq == next_expected:
                                if curr_dup_ack > max_dup_ack:
                                    max_dup_ack = curr_dup_ack
                                curr_dup_ack = 0
                                while(received[next_expected]):
                                    next_expected = next_expected + 1
                            else:
                                nacks[next_expected] = nacks[next_expected] + 1
                                curr_dup_ack = curr_dup_ack + 1
                    more_than_three = len(filter(lambda (k, v): v > 3,nacks.iteritems()))
                    print str.format("{0},{1},{2},{3}", delay, diff, max_dup_ack, more_than_three)
            except:
                print line
                sys.exit(1)
if __name__ == "__main__":
    main(sys.argv[1:])
