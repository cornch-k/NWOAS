# S214 — S208 gap50 diagnostic in the S200 control configuration

The S192 payload and host CPU-init omission are retained; only the HV EOI-PC
diagnostic is corrected. Short integration, 256MiB complete write/read,
2,219 continuous reads in 300.073 s and 31 mixed CPU/read samples over
1,807.56 s passed with acknowledged exit 0. See the JSON evidence files.

The active-read maximum was 993,139 us versus median 125,372 us; this outlier
is unexplained. These are logical reads on a compressed filesystem, not raw
SSD throughput or proof of production stability. The S204/S211 source-prefix
relocation failure was not exercised by this original-prefix control.
