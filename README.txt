# EZgRPC 

Still in its draft.

requires: -lnghttp2 -lpthread

- does not support over TLS.
- does not support streaming messages.
- only works on unix machine with poll.

Even though it's marketed as `EZ`, you'd have to deal with serialization
for each service.

Requires further testing.

