The goal of this project is to hold the code to verify a nonce.
In practice, objects here mangle an 80 byte block already including the nonce found by hashing.

This is decoupled from the core hashers mostly as means to provide a modular testing framework.
Besides, those are assumed to be very stable in terms of changes and they are a decent source of documentation regarding the algorithms themselves.
