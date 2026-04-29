package hfycodec

import (
	"encoding/binary"
	"strings"
	"testing"
	"time"

	"golang.org/x/text/encoding/simplifiedchinese"
)

func TestFormatFixedText095HasNoTail(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"183.141.76.29",
		"475862105",
		false,
		true,
		true,
		true,
		"095",
		false,
	)

	want := "product-demo|2023/12/17 22:01:59|2135/1/1 10:46:14|demo disclaimer|183.141.76.29|0|1|1|183.141.76.29|1|475862105"
	if got != want {
		t.Fatalf("unexpected fixed text:\nwant: %s\ngot:  %s", want, got)
	}
}

func TestFormatFixedText099HasNoTail(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"183.141.76.29",
		"475862105",
		false,
		true,
		true,
		true,
		"099",
		false,
	)

	want := "product-demo|2023/12/17 22:01:59|2135/1/1 10:46:14|demo disclaimer|183.141.76.29|0|1|1|183.141.76.29|1|475862105"
	if got != want {
		t.Fatalf("unexpected fixed text:\nwant: %s\ngot:  %s", want, got)
	}
}

func TestResolveVersionProfileDefaultsTo099(t *testing.T) {
	profile, ok := ResolveVersionProfile("")
	if !ok {
		t.Fatalf("expected default version profile")
	}
	if profile.Code != "099" {
		t.Fatalf("unexpected default version code: %s", profile.Code)
	}
	if profile.ProductName != "099冒险岛-自定义属性" {
		t.Fatalf("unexpected default product name: %s", profile.ProductName)
	}
	if profile.Tail == "" {
		t.Fatalf("expected 099 tail to use default suffix")
	}
}

func TestResolveVersionProfile083ProductName(t *testing.T) {
	profile, ok := ResolveVersionProfile("083")
	if !ok {
		t.Fatalf("expected 083 version profile")
	}
	if profile.ProductName != "083山茶冒险岛-装备扩展" {
		t.Fatalf("unexpected 083 product name: %s", profile.ProductName)
	}
	if profile.Tail == "" {
		t.Fatalf("expected 083 tail to exist")
	}
}

func TestFormatFixedText083IncludesTailWhenEnabled(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"183.141.76.29",
		"475862105",
		false,
		true,
		true,
		true,
		"083",
		true,
	)

	if !strings.HasSuffix(got, fixedTextAuthorizationSuffix083) {
		t.Fatalf("expected 083 fixed text to include tail")
	}
}

func TestFormatFixedText099IncludesDefaultTailWhenEnabled(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"183.141.76.29",
		"475862105",
		false,
		true,
		true,
		true,
		"099",
		true,
	)

	if !strings.HasSuffix(got, fixedTextAuthorizationSuffix) {
		t.Fatalf("expected 099 fixed text to include default tail")
	}
}

func TestFormatFixedTextUsesServerIPAsSecondIP(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"10.10.10.10",
		"475862105",
		false,
		true,
		true,
		true,
		"099",
		false,
	)

	if !strings.Contains(got, "|183.141.76.29|0|1|1|10.10.10.10|1|475862105") {
		t.Fatalf("expected fixed text to use server ip as second ip, got: %s", got)
	}
}

func TestBuildEncryptedFromTemplateBytes(t *testing.T) {
	key := []byte("heifengye111")

	oldBlob, err := simplifiedchinese.GBK.NewEncoder().Bytes([]byte("old content"))
	if err != nil {
		t.Fatalf("encode old blob: %v", err)
	}

	templatePlain := make([]byte, 0, 128)
	templatePlain = append(templatePlain, []byte{0x11, 0x22, 0x33, 0x44}...)
	templatePlain = append(templatePlain, []byte(strings.Repeat("0", 64))...)

	lengthBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(lengthBytes, uint32(len(oldBlob)))
	templatePlain = append(templatePlain, lengthBytes...)
	templatePlain = append(templatePlain, oldBlob...)
	templatePlain = append(templatePlain, []byte("TAIL-DATA")...)

	templateEncrypted := Encrypt(templatePlain, key)

	fixedText := "product-demo|2023/12/17 22:01:59|2135/1/1 10:46:14|demo disclaimer|183.141.76.29|1|0|1|183.141.76.29|0|475862105"
	out, err := BuildEncryptedFromTemplateBytes(templateEncrypted, fixedText, key)
	if err != nil {
		t.Fatalf("build encrypted bytes: %v", err)
	}

	plain := Decrypt(out, key)
	if len(plain) < 72 {
		t.Fatalf("plain too small: %d", len(plain))
	}
	if string(plain[:4]) != string([]byte{0x11, 0x22, 0x33, 0x44}) {
		t.Fatalf("magic mismatch")
	}

	newLen := binary.LittleEndian.Uint32(plain[68:72])
	newBlob := plain[72 : 72+newLen]
	decodedBlob, err := simplifiedchinese.GBK.NewDecoder().Bytes(newBlob)
	if err != nil {
		t.Fatalf("decode new blob: %v", err)
	}
	if string(decodedBlob) != fixedText {
		t.Fatalf("blob mismatch")
	}

	if gotTail := string(plain[72+newLen:]); gotTail != "TAIL-DATA" {
		t.Fatalf("tail mismatch: %q", gotTail)
	}
}

func TestBuildEncryptedFixedText(t *testing.T) {
	key := []byte("heifengye111")
	fixedText := "product-demo|2023/12/17 22:01:59|2135/1/1 10:46:14|demo disclaimer|183.141.76.29|0|1|1|183.141.76.29|1|475862105"

	out, err := BuildEncryptedFixedText(fixedText, key)
	if err != nil {
		t.Fatalf("build encrypted fixed text: %v", err)
	}

	plain := Decrypt(out, key)
	if len(plain) < 72 {
		t.Fatalf("plain too small: %d", len(plain))
	}

	if gotMagic := binary.LittleEndian.Uint32(plain[:4]); gotMagic != FixedMagic {
		t.Fatalf("magic mismatch: got %d want %d", gotMagic, FixedMagic)
	}

	newLen := binary.LittleEndian.Uint32(plain[68:72])
	newBlob := plain[72 : 72+newLen]
	decodedBlob, err := simplifiedchinese.GBK.NewDecoder().Bytes(newBlob)
	if err != nil {
		t.Fatalf("decode new blob: %v", err)
	}
	if string(decodedBlob) != fixedText {
		t.Fatalf("blob mismatch")
	}

	if gotTail := plain[72+newLen:]; len(gotTail) != 0 {
		t.Fatalf("tail mismatch: got %d bytes", len(gotTail))
	}
}
