# AUP-SMS integration

This directory is the stable integration boundary between CBM Lens and
AUP-SMS. `cbm-lens-embed` owns only the same-origin iframe lifecycle and the
versioned message contract. It does not import, fork or duplicate the Lens
React application.

The host serves these ES modules from its authenticated Lens gateway and sets
`activation-url`, `generation` and `contract-version="1.0"` on the custom
element. The upstream standalone Lens remains unchanged.
