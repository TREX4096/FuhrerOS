# F-107 — TCP connection stalled forever

**Symptom:** Loopback TCP streams stopped with a full receive buffer even after the reader drained it.
**Reproduction:** transition network phase under the adaptive scheduler
**Expected:** the sender resumes when the reader drains
**Observed:** the sender waited forever
**Root cause:** The receiver never advertised the reopened window, and with nothing unacknowledged the sender had no timer running.
**Fix:** Window-update ACK when a nearly full buffer is drained; persist timer with 1-byte zero-window probes.
**Regression test:** netdbg suite: 0 retransmissions, flows continue.
**Lesson:** Flow control needs both a window update and a persist timer.
