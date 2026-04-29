package licenses

import (
	"testing"
	"time"
)

func TestApplyDuration(t *testing.T) {
	start := time.Date(2026, 4, 25, 12, 0, 0, 0, time.Local)

	testCases := []struct {
		name string
		unit string
		want time.Time
	}{
		{name: "day", unit: "day", want: start.AddDate(0, 0, 1)},
		{name: "week", unit: "week", want: start.AddDate(0, 0, 7)},
		{name: "month", unit: "month", want: start.AddDate(0, 1, 0)},
		{name: "year", unit: "year", want: start.AddDate(1, 0, 0)},
	}

	for _, tc := range testCases {
		tc := tc
		t.Run(tc.name, func(t *testing.T) {
			got, err := ApplyDuration(start, 1, tc.unit)
			if err != nil {
				t.Fatalf("ApplyDuration returned error: %v", err)
			}
			if !got.Equal(tc.want) {
				t.Fatalf("unexpected result: want %v, got %v", tc.want, got)
			}
		})
	}
}
