package web

import "testing"

func TestNormalizeLicenseIP(t *testing.T) {
	testCases := []struct {
		name string
		raw  string
		want string
	}{
		{name: "ipv4 loopback", raw: "127.0.0.1", want: localLicenseIP},
		{name: "ipv6 loopback", raw: "::1", want: localLicenseIP},
		{name: "localhost", raw: "localhost", want: localLicenseIP},
		{name: "public ip", raw: "43.230.72.10", want: "43.230.72.10"},
	}

	for _, tc := range testCases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			if got := normalizeLicenseIP(tc.raw); got != tc.want {
				t.Fatalf("normalizeLicenseIP(%q) = %q, want %q", tc.raw, got, tc.want)
			}
		})
	}
}
