# Udp Test
A small, reliable messeging layer on top of udp

## Todo
- multiple arks (done)
- ping / package loss tracking (done)
- congestion avoidance
- connection resetting
- sequence wrapping

## Issues to investigate
- is loss underreported? It should not be 0000 over remote connenction
- sometimes long startup times
- doesnt work on windows (could be related to startup times)
