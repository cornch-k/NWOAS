# S137 vGIC disable trace

S137 preserves S133 interrupt behavior and logs each effective SPI disable as
`[spi-dis]`. It exists to identify which Windows interrupt transitions occur
before the Welcome-screen stall. It does not apply S136's broad AIC mask
change.
