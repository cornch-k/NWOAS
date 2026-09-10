# Main resolution of S225 review

F1/F2: Header and README now document capacity/MDTS constraints and that result
fields must be interpreted only on return 1; return 0 is not a completion.
F3: Import-free is explicitly a freestanding ARM64 build property requiring
-ffreestanding -fno-builtin plus the undefined-symbol check. Hosted tests may
use compiler-generated libc calls.
F4: Added a reproducible 48,864-command test over twelve policy profiles,
including ReadOnlyNamespace, single/paired modes, MDTS 0/4/8, all CNS/LID bytes,
untouched output tails, and fully randomized command bytes.
F5: Added zero primary capacity, paired link >=2^40 and MDTS 9 rejection cases.
F6: README names the inherited live CachedPair admin path and its unapplied
TransportReadOnly wrapper. S225 remains uninstalled and source-only.

The review's separate 145,728-command run is attributed to the reviewer; the
checked-in main tests separately record 12,182 + 48,864 commands. No response
construction logic needed changing.
