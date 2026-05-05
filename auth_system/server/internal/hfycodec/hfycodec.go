package hfycodec

import (
	"crypto/md5"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"os"
	"strings"
	"time"

	"golang.org/x/text/encoding/simplifiedchinese"
)

const FixedMagic uint32 = 89742336

var versionTailPlaintext = strings.Join([]string{
	"00AD8580,00BE4F24,00BF3CD8,00D8A1EC,00E32F74,00F6D130",
	"00AD8530,00BE4ED4,00BF3C6C,00D8A188,00E32F10,00F6D0BC",
	"004F1DB5,009AFEEF,009A3D81,00AFA655,00AB167A,00B67E39",
	"0081324A,0050D7A6,009516C2,00545476,00B0DEA9,00BC3A78",
	"0070EC25,00A00FA0,009536E0,00A911A3,00A5EEDD,00BC3A7D",
	"00A10FB8,00A00FA5,008AD01A,009D2B67,00A2B828,00B1086E",
	"005BD868,008BE044,0077E055,009E86FB,009416A0,00AD85D8",
	"00719DA7,008C8BAF,00AFE8A0,00C8C5F8,00D021B8,009D9CE0",
	"008E0C06,00B064B8,0079023C,0084D268,00603071,009F6C8D",
	"008F36FA,0079645B,007806D0,0066E565,005C597F,0062ACD1",
}, "\r\n")

func FormatLicenseTime(t time.Time) string {
	return fmt.Sprintf("%04d/%d/%d %02d:%02d:%02d",
		t.Year(),
		int(t.Month()),
		t.Day(),
		t.Hour(),
		t.Minute(),
		t.Second(),
	)
}

func FormatFixedText(
	productName string,
	addedAt, expiresAt time.Time,
	disclaimer, boundIP, serverIP, boundQQ string,
	param1, param2, param3, param4 bool,
	versionCode string,
	appendVersionTail bool,
) string {
	firstHfyIP := strings.TrimSpace(boundIP)
	secondHfyIP := strings.TrimSpace(serverIP)
	if secondHfyIP == "" {
		secondHfyIP = firstHfyIP
	}
	profile, hasProfile := ResolveVersionProfile(versionCode)
	repeatedHfyIP := secondHfyIP
	if hasProfile && profile.SecondIPSuffix != "" {
		repeatedHfyIP += profile.SecondIPSuffix
	}

	parts := []string{
		productName,
		FormatLicenseTime(addedAt),
		FormatLicenseTime(expiresAt),
		disclaimer,
		firstHfyIP,
		boolDigit(param1),
		boolDigit(param2),
		boolDigit(param3),
		repeatedHfyIP,
		boolDigit(param4),
		boundQQ,
	}

	if appendVersionTail {
		if hasProfile && profile.HasTail {
			if tail := BuildVersionTail(secondHfyIP, profile.TailPrefix); tail != "" {
				parts = append(parts, tail)
			}
		}
	}

	return strings.Join(parts, "|")
}

func BuildVersionTail(boundIP, prefix string) string {
	boundIP = strings.TrimSpace(boundIP)
	if boundIP == "" {
		return ""
	}

	prefix = normalizeTailPrefix(prefix)
	key := []byte(boundIP + prefix)
	encoded := versionTailCrypt([]byte(versionTailPlaintext), key)

	var out strings.Builder
	out.Grow(2 + len(encoded)*2)
	out.WriteString(prefix)
	for _, b := range encoded {
		out.WriteByte('A' + (b >> 4))
		out.WriteByte('A' + (b & 0x0F))
	}
	return out.String()
}

func normalizeTailPrefix(prefix string) string {
	prefix = strings.ToUpper(strings.TrimSpace(prefix))
	if len(prefix) >= 2 {
		return prefix[:2]
	}
	return randomTailPrefix()
}

func randomTailPrefix() string {
	const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

	buf := make([]byte, 2)
	if _, err := rand.Read(buf); err != nil {
		now := time.Now().UnixNano()
		return string([]byte{
			alphabet[int((now>>8)%26)],
			alphabet[int((now>>16)%26)],
		})
	}

	return string([]byte{
		alphabet[int(buf[0])%26],
		alphabet[int(buf[1])%26],
	})
}

func BuildEncryptedFromTemplatePath(templatePath string, fixedText string, key []byte) ([]byte, error) {
	templateBytes, err := os.ReadFile(templatePath)
	if err != nil {
		return nil, fmt.Errorf("read template: %w", err)
	}
	return BuildEncryptedFromTemplateBytes(templateBytes, fixedText, key)
}

func BuildEncryptedFixedText(fixedText string, key []byte) ([]byte, error) {
	encoder := simplifiedchinese.GBK.NewEncoder()
	newBlob, err := encoder.Bytes([]byte(fixedText))
	if err != nil {
		return nil, fmt.Errorf("encode fixed text to gbk: %w", err)
	}

	newDigest := md5HexLower(key) + md5HexLower(newBlob)
	outPlain := make([]byte, 0, 4+64+4+len(newBlob))

	magicBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(magicBytes, FixedMagic)
	outPlain = append(outPlain, magicBytes...)
	outPlain = append(outPlain, []byte(newDigest)...)

	lengthBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(lengthBytes, uint32(len(newBlob)))
	outPlain = append(outPlain, lengthBytes...)
	outPlain = append(outPlain, newBlob...)

	return Encrypt(outPlain, key), nil
}

func BuildEncryptedFromTemplateBytes(templateBytes []byte, fixedText string, key []byte) ([]byte, error) {
	plain := Decrypt(templateBytes, key)
	if len(plain) < 72 {
		return nil, fmt.Errorf("template plaintext too small")
	}

	oldLen := binary.LittleEndian.Uint32(plain[68:72])
	if int(72+oldLen) > len(plain) {
		return nil, fmt.Errorf("template plaintext is malformed")
	}

	magic := append([]byte(nil), plain[:4]...)
	tail := append([]byte(nil), plain[72+oldLen:]...)

	encoder := simplifiedchinese.GBK.NewEncoder()
	newBlob, err := encoder.Bytes([]byte(fixedText))
	if err != nil {
		return nil, fmt.Errorf("encode fixed text to gbk: %w", err)
	}

	newDigest := md5HexLower(key) + md5HexLower(newBlob)
	outPlain := make([]byte, 0, 4+64+4+len(newBlob)+len(tail))
	outPlain = append(outPlain, magic...)
	outPlain = append(outPlain, []byte(newDigest)...)

	lengthBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(lengthBytes, uint32(len(newBlob)))
	outPlain = append(outPlain, lengthBytes...)
	outPlain = append(outPlain, newBlob...)
	outPlain = append(outPlain, tail...)

	return Encrypt(outPlain, key), nil
}

func Decrypt(data []byte, key []byte) []byte {
	return crypt(data, key, 4)
}

func Encrypt(data []byte, key []byte) []byte {
	return crypt(data, key, 4)
}

type rc4State struct {
	s [256]byte
	i byte
	j byte
}

func rc4Init(key []byte) rc4State {
	var st rc4State
	for idx := range st.s {
		st.s[idx] = byte(idx)
	}

	j := 0
	keyIndex := 0
	for i := 0; i < 256; i++ {
		j = (int(st.s[i]) + int(key[keyIndex]) + j) & 0xFF
		st.s[i], st.s[j] = st.s[j], st.s[i]
		keyIndex = (keyIndex + 1) % len(key)
	}

	return st
}

func rc4Skip(st *rc4State, n int) {
	for step := 0; step < n; step++ {
		st.i++
		st.j += st.s[st.i]
		st.s[st.i], st.s[st.j] = st.s[st.j], st.s[st.i]
	}
}

func rc4Xor(buf []byte, st *rc4State) []byte {
	out := append([]byte(nil), buf...)
	for idx := range out {
		st.i++
		st.j += st.s[st.i]
		tmp := st.s[st.j]
		st.s[st.j] = st.s[st.i]
		st.s[st.i] = tmp
		keyIndex := (int(st.s[st.j]) + int(tmp)) & 0xFF
		out[idx] ^= st.s[keyIndex]
	}
	return out
}

func key32(raw []byte) []byte {
	hexBytes := []byte(md5HexLower(raw))

	for i := 0; i < 32; i += 2 {
		hexBytes[i], hexBytes[i+1] = hexBytes[i+1], hexBytes[i]
	}

	for i := 0; i < 16; i++ {
		hexBytes[i], hexBytes[31-i] = hexBytes[31-i], hexBytes[i]
	}

	return hexBytes
}

func crypt(data []byte, key []byte, initPos int) []byte {
	out := append([]byte(nil), data...)
	a2 := 0
	a4 := len(out)
	ptr := 0

	if initPos > 0 {
		ptr = initPos
		a4 -= initPos
		a2 = initPos
	}

	st := rc4Init(key)
	blockIndex := a2 / 4096
	rc4Skip(&st, 4*blockIndex)

	blockRandoms := rc4Xor(make([]byte, 4*(a4/4096)+8), &st)
	rpos := 0
	off := a2 % 4096
	k := key32(key)

	mk := func(rand uint32, idx uint32) rc4State {
		buf := make([]byte, 0, 4+len(k)+4)
		tmp := make([]byte, 4)
		binary.LittleEndian.PutUint32(tmp, rand)
		buf = append(buf, tmp...)
		buf = append(buf, k...)
		binary.LittleEndian.PutUint32(tmp, rand^idx)
		buf = append(buf, tmp...)
		return rc4Init(buf)
	}

	if off > 0 {
		rand := binary.LittleEndian.Uint32(blockRandoms[rpos : rpos+4])
		rpos += 4

		s := mk(rand, uint32(blockIndex))
		blockIndex++

		rc4Skip(&s, off+36)
		take := min(4096-off, a4)
		chunk := rc4Xor(out[ptr:ptr+take], &s)
		copy(out[ptr:ptr+take], chunk)
		ptr += take
		a4 -= take
	}

	for a4 > 0 {
		rand := binary.LittleEndian.Uint32(blockRandoms[rpos : rpos+4])
		rpos += 4

		s := mk(rand, uint32(blockIndex))
		blockIndex++

		rc4Skip(&s, 36)
		take := min(4096, a4)
		chunk := rc4Xor(out[ptr:ptr+take], &s)
		copy(out[ptr:ptr+take], chunk)
		ptr += take
		a4 -= take
	}

	return out
}

func md5HexLower(raw []byte) string {
	sum := md5.Sum(raw)
	return hex.EncodeToString(sum[:])
}

func versionTailCrypt(data, key []byte) []byte {
	if len(data) == 0 || len(key) == 0 {
		return nil
	}

	s := make([]byte, 256)
	for idx := range s {
		s[idx] = byte(idx)
	}

	j := 0
	for i := 1; i <= 256; i++ {
		j = ((j + int(s[i-1]) + int(key[(i-1)%len(key)])) % 256) + 1
		s[i-1], s[j-1] = s[j-1], s[i-1]
	}

	out := make([]byte, len(data))
	i := 0
	j = 0
	for idx, b := range data {
		i = ((i + 1) % 256) + 1
		j = ((j + int(s[i-1])) % 256) + 1
		s[i-1], s[j-1] = s[j-1], s[i-1]
		t := ((int(s[j-1]) + int(s[i-1])) % 256) + 1
		out[idx] = b ^ s[t-1]
	}
	return out
}

func boolDigit(value bool) string {
	if value {
		return "1"
	}
	return "0"
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
