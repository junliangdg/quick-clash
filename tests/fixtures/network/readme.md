# Public local TLS fixture

server_cert.pem and server_key.pem are intentionally public test data for the local integration server. They are not production credentials and must never be used for a real service. The certificate covers the reserved fixture.quickclash.invalid name and the loopback subscription endpoint.

The Qt test process alone trusts the certificate. The native WinINet probe permits the unknown fixture CA for its own request only; it does not change Windows trust or product TLS policy. All fixture listeners bind loopback and use bounded lifetimes. See docs/baseline/test_fixtures.md for scope and recorded evidence.
