# Architecture

See [phase1.md](phase1.md) for requirements, source conflicts, interfaces and acceptance gates.

`ProtocolConfig` validates versioned JSON and provides a stable value snapshot. `ConversionEngine` owns the upstream stream cache, parses a complete external frame, evaluates bounded mapping expressions and produces a protocol neutral `UnifiedCommand`. Its preview uses a fresh `QushengProtocol`, so it cannot alter the sequence number of a live connection. `Widget` holds the enabled configuration and one `ConversionEngine` per upstream serial connection; editing the JSON text changes only the draft.

`TranslationService::submitUnifiedCommand` submits a mapped command to `RequestManager`. The manager writes through `ITransport` and separates write completion, 0x11 command acceptance, and motor completion. After 0x11 acceptance it polls 0x12 and checks both idle state and requested absolute position. A 0x17 report updates busy status but cannot complete a request because it has no request sequence. Commands are serialized and motions are never retried automatically. Cancelling a local wait leaves the motor state busy/unknown until a query confirms idle.

`Widget` owns separate serial transports for the upstream external connection and downstream device connection. It records an upstream connection generation and configuration snapshot per accepted request, so a reply returns to the original live connection and a config edit cannot change its format. The bridge and real send controls require explicit enabling and confirmed device/calibration/CRC flags. Preview never sends.

`src/demo` and `DtClientProtocol` remain as compatibility/test components. The application uses the configuration engine as its production input path. TCP transport, complex action orchestration and editable downstream templates are extension points, not supported by this phase.
