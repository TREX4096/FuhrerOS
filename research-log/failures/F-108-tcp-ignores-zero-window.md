# F-108 — 20x lower TCP throughput under the adaptive scheduler

**Symptom:** Network phase: about 40 sends per 250 ms under adaptive vs about 1300 under round-robin; 17-18 retransmissions per connection.
**Reproduction:** netdbg suite
**Expected:** comparable throughput
**Observed:** 20x slower, timeout driven
**Root cause:** When the peer advertised a zero window the sender still sent one MSS (a fallback in tcp_output), which the receiver dropped; without fast retransmit every drop cost a 300 ms timeout. Round-robin drained the receiver before the window closed, hiding the bug; the adaptive policy delayed the consumer and exposed it.
**Fix:** Respect a zero window (probe instead) and fast retransmit on 3 duplicate ACKs.
**Regression test:** netdbg after the fix: about 124 MB per connection under adaptive vs about 94 MB under round-robin, 0 retransmissions.
**Lesson:** A different scheduler is a good way to shake out protocol bugs; check the mechanism before blaming the policy.
