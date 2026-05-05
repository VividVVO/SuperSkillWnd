package hfycodec

import (
	"encoding/binary"
	"strings"
	"testing"
	"time"

	"golang.org/x/text/encoding/simplifiedchinese"
)

const (
	sampleTail083    = `ENLILPIHHPJBGGOEFOEFCOONHAGDKKODIHHGNOMFAIHOLJNOEDKHHKPDIDDPHAFEPOHJDEGDICIALMGPAFEJNPEFOLDCIGJHKCHICNDLNGMMPEHGBADJLDFHGBCHFAFHCJHDHDKAILOEGCDHIALHHKPPGGKNJJDLGKGMCIAPAEPEPHPOFLBLKKDBGBGDKPGBHIAMCFHAKOJFNOGJPMMCAPPHHHKMFDHIFAFJMJOPIHCFOKBNNOJAMKBCDEKKIKOFFICLJJKPOLMNGMCKCFNBBMBAMIBPNFIAJOBIMHEDNPOBGGODIKOAOHHFGBAHLFCPDMBNOPELCKBHPIHHALFPBKPGIPBIBMEFKIAAMHMAJEHNLLGKHAINEJIDCDFNBACBACCLGNJCAGHFJIMKBKEEPJCMFCFGOIAMMHHMFNHLPBHMOALEIJFCPPFFIOJJODFLMEFPKCLLPMMNNAEPHLLEIHHDBOGFPHICKIEPLNJHILFJOGOPBCMIEKJFBDDMPPKFMDHJHHHKOLPKMBOCFIIEGFJCDJFKOHNBBCJGAEIDKALJLBKAIMKKLPLFAODADKMNIOJPBMHOBBOIAIGMLBLMELKEMOFOEKDFDOANIBIOPJGICFPNLHGDIDFNCMNBLGBNONGLPNEFINKPJGIBHLCBGAHGMCKCHMGGIBEIPKFCGHMMFEBGGGOJOADKHBNGAJJPFJBABDFLGMNJOJMFMEELGBGAEMHLAPMFKBKDLKDNHCNHGCEDPAIPCJEGEAGOMDOFMBCLKCAHINEGOCPMKPPMOJIJCOFJCMHDGMGKIHJLGOHNMDOCIIHHLBLCJLFCIHOJIPGGCKFPEAAGAIOKHGEJLAGKHMHDDOPGAEHOHCPCDCOEMEMGNEKCFJEKFNNAMCGOIJJAPHHNJHNBHNDOICEJABNPPFONKGILFJHNNDGMHEEFHPEJDDNCOCBNLMKFPEGNCNOGEMIKLLDBOJJHPGOJFPIMEDKDNAEBMHGGBHGNANJFBOJNPOHGEGHBGBMKIJFGLOJHIJDFGDIPBPMKAJMGMMLEOIHKGIMFICPFHMNALGCOPCNGIMHDLANBNHNIDCFLDAOBJMLNBO`
	sampleTail085    = `XFPMOFOBIPHMFPLAJLMHMOCBFEEHJCBEHFFCHIDOLCFIIAJMFDBENNOJFBDLDNJLCGAEEHCEHPKBJINKHLGACKDCAPADLKMNJDHIFKLOIFLADONPLKEGMFEFFPKAEJJDIBOLBGNJCELOFDPMLEBMKCBJCGELOLGDPKEFIOGEGPLIPMBCIJOFKOGHCDFKJGANEPCBNBMBLNHBPFIHKGECOELBLGBCGEAIEHDKDEDEMAKCNFAKJCBBGPJLEHMHHJLBAHICAOGOHFCENHIDHNABHNKODAABHHMLDDHHALOLGDIPLBHPKMIBPGEENNMBKAEGGNGANFFCEJLHIHEGDKEPMJBEIOEBDBFEEADMJKHHIDKBCLKGOGDNEKIPEGHIBJBCBEAJCKHDDKKBJLCNLKKEAADDEAKLPHJBLOIGOPOGENDMKPBDNAJKCCPCBLHNEBIPFJPMDMJOHFENDEBFDPAPKBHHMHEMJKHNKNKHKEEIEKINBMJGBJIHMBNMJIEAAMEAJHDHHDGOFGIIKBMPDHHGNPFCJABOFLJELAODJBLCELLNHEGPPHLNCMGOMNJHONNEFHEJMOABENKDHDJFJCGAFEPBENOHLEFCDBEDNIALABABIJHGFAFANJOEADFHOBPBHNGENOCDMJGPGNOBOKHABMPFLCFBMOKGMBNACOINFIGGNPGGHFDKAIKPGIHLLEOAFKHJMLLIPEDJKDENNBJMEFAAILNMNBMOIDKAJHGLABGCGFAAGAHBOAPAIOHNEKKNINJMLCGHICILDPBJOPANONCNNKDAJEFADPPOEIKLHEBPJIMIJFLDDLDKHEECJJPAIKDDHJLCIEGNMKEMMAKNLCHKAIKPGFBEEGBEDAMHCHOEJKKPJIKJHHKNFFDPLBNNOFAFFOCPBCFENBHAEDBFMPCFOBLNIDMDKFIKKJMDLJOPJDKIBKCDNNIEMNHHKNOHFIOBNJABGFLBPBCLMPICOEHNNJOKJBHDCAJEMAPAKENEAFBGCOFHGONHCCNMBMOLMPEFEBCFCALKDCKBONLJFGBFKHPKJBCGFPPDJMHEILKOJMCOHBDCJNDBGHFGPCFLIBJJJDCOHJ`
	sampleTail085816 = `LCOKHBDLPOLIIPFMEPKNHHHFPMONEDHAMCPJLGEGKFFPHLALNHPIOGLPBBMBNDNGMKAKODAHLPGONEGILKMOFLLLIEBLEEKJABMABDIFDLAILFONOHGPGOPKINOAMFGBEINEDGJLDBAOPDCNDMCFIDHCIHPBGMDMDGDEHDHFOLEHLHEOMMHFDMBONAPPEKPJEDJAOFNJPCCMHJCFLAFDNMHFHHHHLKHPPDIDJKKGCCFFJHNBNDMEGJCEGEIPMNCEMBECAKFLFCMAJDKIDGEDMAPOLLFCPKMMOEDMCOKHDKMPFGLJPDJNEFDNHABNDJMPHHOHKCJOAFAOFPEANPEGIOMAPNAAIIKBEJKAEOPHAPGALPHIJHMMNEIJNDIOEGKMBIGJOHANPIKGMNKDAPBADGLIPGJEBLMFJIJOMDKIPIMBMGPFPKCMLDAMMGCGBBMGEIBHDMMCNJMLBEHMOCENCPIIGBBBIJCCMCGJKEDHBOOEMLMIPGCBBFBAPBDBGPAFGNOECJGPOFNIMGGLCCNBGNDKGGOKGMEFLCOGDBMJDFEHIDDHFDPPJLFIIEEBIDHHEANCDDMMPBHINABCGJGMPOGAMHCPLCCFGPNDCOFGLBAGHCJGHENBOEFLCCJPOJLHLEONLAJNNOPAPIPJMDDFEDFNBGKDGNDBDNKADNENHPBGAFONIDPDOAGODBJIPPMLFLEHBBBCAENGLHAILKDGMEHLOMBJIACFFIJHDOPOEPMCEEFAJJEKDOAHLNCCOGLGEABKFLPAOFCPDAGDBJOJOMHHLFDLPECJLLHCEIKHDDPDNNMOLPHJGCPGJGDFFICKIFMCAEDMAJMMOBMMOOGECFLNMOEGENILDJHPEEPJHNBHDNFBALIBHEPPNAKHMAEJMBOHCGJDCIHAOCMCJMPIGEGJIMBCBCAOPJNBLEMLNIOJPMAIPHAGCICAAAIBOLEBCOIAJMHEKONJCGAINMIKLANBIMABAMEBOCAPPKDLKLNDMDOCCDMBPAFIANBIOAINMPFJFIPJANCFLCKLDGJJAHNLKLJIAEDHFDPGLINBBDOEJBPLMBJACJLONHAOCPEKHEFHKHPDCO`
	sampleTail095    = `VSDIPPBCDJFJFJJLNCMBDNDPMOGONODNHDDKFEKIMKHMCFKONCMAMLLDCJIDPADIEGKHLNPPHMKAIKOGJJCFIMPPBBMAKGHPJJEBIOKKOIFMMNCADFKDNAOMAPEAHOPLOHDAFOOLNOBJLPPJKMECMKFKNHBCIBAGEDOABMBJFANMKBAGHLKLKGKJDGNBLDDFIENNBPAJAHPODFCKAJDCBEBKEJFNGNPDDNIMPPKDLELLLILLDBLHLMBMDMLPFCNEDMKIDEHCAENMGNNECILCMLGIOAPDFPMFDHPKLBEJMHHMBKLFALIIHJBDIOMCLPDNBDCIAJNDCGMOODAHPFDMNCGGGNDHFGONCLKHDCKKEFJPLANAKNMEKLKDNIPKNGAGEELECMIMGMFFJBECBPBGGNCMJGNGBJFKHAFBIIPJFLPMPIKMMOBPLBHPKEDFBEDIBMEOPAOFJEBEIHBCADMFMMHCDHFLMPCGPAAJMAJGBBGIFADCIBNHADJHPOBDPJDBHDBFFGCPEFCIOGIGOILPEDPKAGDFLAJLKKBMEHIDJPMOMMMHFKGMKCOFILOPMNELPBHDFPIOIMJIFFELBMGAPLBJKEEDIAPABCCGLPHFOBEDPMBNBMJGMBNKEHPCKCCELKHLGKKKLBBKDOCMEJOHGKJNDFCOJOJABLLKPJDMOBPLKBPEGFLEPDPIPDPCFKFHCNPIHPMGPBCJIHPMDGLAHDJPPOGAFBMDGHBFNOHEPPLBGEOBMIMEGBONCKLBPLEHCLNKJGEJJLOFGDCHJEAIPDBLOEJKFDBBMILENNGNHBEHJFFMDLCOBJAANPAOKJOOMMCAFHJELKNECHECIAAIIALADEHHMNKBECDBLHFJJCENKKEPNEOGHJDCFAGOMGFCHFNPCIKKLGPHAOBHMNGJMGCILOFFPCBGMABJEPOMBHKNCBPIMLLEFNBCHGGOGAFPAPDJKEIOBGIIHKBMHPFEHNPDDNDDDDLDKNCDGMBDCJJIAMEFIFNGCMJIEEMALLGAJGKFHLDJHKNLEBBGKNNNIIDPMNEGCJNPJHLHNLMJFNBJMPIFFPEJGOFFCNMIKJHAIDJBKLHJLA`
	sampleTail095B   = `VSMCDHAEAGDMMDHCDPJOGOOIBCOJJNHMNDMNKAKDPGPJLNJMBEDNIFMIKMNKGOFICDFFOIOCIPNKKDDGDPCFIDJJJJGLPFKBEDOMNEEBPHLDJDNCIPGHMHLABIBBBCCAGGMKEANJMPNCNMDEIILFKEKNKOFMOBAPAGLCLLEKCIABEOLCDMIPAIGIFDFEGHBHDLGNHGNILIABMFJLFAABBOEAOKBECPMHOJHFKIIHIIAOMEDFPENEFOFBEOBKOIKBGPAKCMBMGNLKKHIAIINLPHMIGNNJDLIIPLMEHFFDAJKLLBFEBKBCHDPOCCOPOFKOJBAECDABFDAGCALOGGNJDBIBLDAMOHBIFAMGGMCEINLNHOKELJEOMCNOEIALMEIJAHIEJMPGFJICLKKGKLLHHDPDKPPLJKCKPKEFHOOCGKHMAJEPLMECJNDDOAFEAHEIIMMFLEMEJDBBBJHOMKFJEEFIKIKKHPGGJOFBIDHNJGLDBEPDBKMOHPFHGJCLENJNJBIGFALACELLLEGJBOGNFBNBENBHHMODAALAMFHEFBJEKBNILADAILJHOIBAGNPCKIFOMPJJFIJNOFOLELFBGFIJNOOKAPEEAGJGDILNKLLFGOJLPFHFIJCKGPOBBNMCACGDGJFKEIJMKHLPJELDLGBMMOHHAJOCBBOGGCNDFEHGMHKLADPJMLNMFKHLHDCFDFBAICKMNEINJMPBLFIFPIONFNCDENFABHFCHFHDAACPANCDOELGJEAANPFCMJILPEHFJBJKCMJKHFKGCHDIGGAGIDHNNFMMGOFBDPLFGJADINKICGBHLCGPBNJHJALAJIMOHLOHJGNLJMCMNEANJJKNAGBLJAEBHEIKPKEPNDDMCFHKBNHLJONPCLICBGFLEGKHFFMBJFFBLDNJEBPPKPIFAEPEAGEJLJEGKNDIMIIHAEPOHKBIOOIEHKAHPCNIEHFDOMKCIDKOOJGMMJKCFFHLFAAMMOLHHDDPKKBLFEPNPEBKDHDDEIHMMBIJAFIKHDMMCBIBHJANOGNNHIKOBJNOJEJKLNLMBPOEHBAMACFHMIHHINGDKMFLMFMEBDMCJDKKBMHAME`
	sampleTail079    = `VGABPPCKGOLOGJPHDPAKGFBJIJBDDMDOAGGDOGMBBGCACAOJKPMGJHKBHBMEHPLNHIIJCFODFLPLKIJBHGBOLDENPLJLNFOCEFGONIMGKKGGHIDBLOHHAHCGFCNNIHJPOGHBBALOMGJLONBCHCBNHEJMFOIMDOBLFOBPAMKEGOBGHGKFNKHDDBFGNKMKFGPONKEBMACGMGEDONOAOILLPHLNPOPPCCCMHHMLIBFDDOCOGNCHIPKKFCIGCFHCDMMNGNKNGLOAFFBKLGNCFFBBOMNDBFGNLNBPHAOENALMFHHBOAMMBGMMEAJBCBJBKAOOPCBGEPIHFMHFBDOOHLCPBFOEDJCBGOEPNMJDIIBGLFLELOHAJHFCDFPNICMFECBEGKFFNMKOPMLGMLINKDDHNOAIKOCBMINPIACDJFGDCFCKNEPGPOFFENHDDBNLIPILMMHIGFOFEKENOHNLFCDHNFJPKPMOMCMHLMLFIAACNPGDAJEJAPKKJEMGCDKDGBPLIOPDKLIIAGIJGMDLOEDFFCGNFHPOKFFHAGBKBKOEAOJCCPFPJBKKBJFKGBKOFILKGDNPDOIHBLPBKIGHICNBMEBDKMDAIHJDLDOOKNOPCNEOAOMGOEHCMCGAPIMIJFLGLLPCHGCMEHCPLJCFFLELMOLGGHBLLADBNEHDOFCIDICEJPEPHGIPHDAFIOOIHPMELEAOEGLLPKMCHAFNDOJPKHFKPFJHGKJENDGMLKMCHMGKHNOFEEFLDJGNKHGLGLIGKBGBCGGHGCOPMMMKOGJBEDHPEGNADAFLICHHOBJEFBOCBFJCEDGKMCIBMGDFOKABHFCDKPJFBFPAGDMPPICPEKHAIDHNDCFMDMAAPGCFGOBDJKDFBLJKGEHNOAKMIHHNELKBHBEMKBIGOOGBBNIIFPEDNLBAOEHLCCPPFFIBOEBIJDOLLACHENACMJOOGFGBFOMKJMMKNFKFELOPNNCNBEAHHHHEEJIFELHBLGFDJLAKNAHCPFONBKALHCJLEFJGIKLFPAJFJHEHIFHMJEDFMOOEKKIDDCOEGCMIBPIFDIMMGNMJEMGEMMKOIKPHOEDALHDNLILEBP`
)

func extractTailFromFixedText(t *testing.T, fixedText string) string {
	t.Helper()

	parts := strings.Split(fixedText, "|")
	if len(parts) != 12 {
		t.Fatalf("expected 12 fields, got %d: %s", len(parts), fixedText)
	}
	return parts[11]
}

func assertTailDecodesToPlaintext(t *testing.T, boundIP, tail string) {
	t.Helper()

	if len(tail) < 4 {
		t.Fatalf("tail too short: %q", tail)
	}

	prefix := tail[:2]
	body := tail[2:]
	if len(body)%2 != 0 {
		t.Fatalf("tail body length must be even: %d", len(body))
	}

	cipher := make([]byte, len(body)/2)
	for idx := 0; idx < len(body); idx += 2 {
		if body[idx] < 'A' || body[idx] > 'P' || body[idx+1] < 'A' || body[idx+1] > 'P' {
			t.Fatalf("tail body contains non A..P chars: %q", body[idx:idx+2])
		}
		hi := body[idx] - 'A'
		lo := body[idx+1] - 'A'
		cipher[idx/2] = (hi << 4) | lo
	}

	plain := versionTailCrypt(cipher, []byte(strings.TrimSpace(boundIP)+prefix))
	if string(plain) != versionTailPlaintext {
		t.Fatalf("tail does not decode back to fixed plaintext table")
	}
}

func TestFormatFixedText079IncludesTailWhenEnabled(t *testing.T) {
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
		"079",
		true,
	)

	wantPrefix := "product-demo|2023/12/17 22:01:59|2135/1/1 10:46:14|demo disclaimer|183.141.76.29|0|1|1|183.141.76.29|1|475862105|"
	if !strings.HasPrefix(got, wantPrefix) {
		t.Fatalf("unexpected fixed text prefix:\nwant prefix: %s\ngot:         %s", wantPrefix, got)
	}
	assertTailDecodesToPlaintext(t, "183.141.76.29", extractTailFromFixedText(t, got))
}

func TestFormatFixedText099HasNoTailWhenDisabled(t *testing.T) {
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

func TestBuildVersionTailMatchesSamples(t *testing.T) {
	cases := []struct {
		name    string
		ip      string
		prefix  string
		exactly string
	}{
		{name: "083", ip: "103.45.130.141", prefix: "EN", exactly: sampleTail083},
		{name: "085-8.1", ip: "109.244.40.138", prefix: "XF", exactly: sampleTail085},
		{name: "085-8.16", ip: "109.244.40.137", prefix: "LC", exactly: sampleTail085816},
		{name: "095", ip: "43.230.72.10", prefix: "VS", exactly: sampleTail095},
		{name: "095-43.241.19.74", ip: "43.241.19.74", prefix: "VS", exactly: sampleTail095B},
		{name: "079", ip: "103.45.131.137", prefix: "VG", exactly: sampleTail079},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := BuildVersionTail(tc.ip, tc.prefix)
			if got != tc.exactly {
				t.Fatalf("tail mismatch for %s", tc.name)
			}
		})
	}
}

func TestResolveVersionProfileDefaultsTo095(t *testing.T) {
	profile, ok := ResolveVersionProfile("")
	if !ok {
		t.Fatalf("expected default version profile")
	}
	if profile.Code != "095" {
		t.Fatalf("unexpected default version code: %s", profile.Code)
	}
	if profile.ProductName != "095" {
		t.Fatalf("unexpected default product name: %s", profile.ProductName)
	}
	if !profile.HasTail {
		t.Fatalf("expected 095 to enable version tail")
	}
	if profile.TailPrefix != "" {
		t.Fatalf("expected 095 tail prefix to be randomized at runtime, got %q", profile.TailPrefix)
	}
	if profile.SecondIPSuffix != " " {
		t.Fatalf("expected 095 second ip suffix to preserve one trailing space")
	}
}

func TestResolveVersionProfile083ProductName(t *testing.T) {
	profile, ok := ResolveVersionProfile("083")
	if !ok {
		t.Fatalf("expected 083 version profile")
	}
	if profile.ProductName != "083" {
		t.Fatalf("unexpected 083 product name: %s", profile.ProductName)
	}
	if !profile.HasTail {
		t.Fatalf("expected 083 to enable version tail")
	}
	if profile.TailPrefix != "" {
		t.Fatalf("expected 083 tail prefix to be randomized at runtime, got %q", profile.TailPrefix)
	}
}

func TestResolveVersionProfile085ProductNameAndTail(t *testing.T) {
	profile, ok := ResolveVersionProfile("085")
	if !ok {
		t.Fatalf("expected 085 version profile")
	}
	if profile.ProductName != "085" {
		t.Fatalf("unexpected 085 product name: %s", profile.ProductName)
	}
	if !profile.HasTail {
		t.Fatalf("expected 085 to enable version tail")
	}
	if profile.TailPrefix != "" {
		t.Fatalf("expected 085 tail prefix to be randomized at runtime, got %q", profile.TailPrefix)
	}
	if profile.SecondIPSuffix != " " {
		t.Fatalf("expected 085 second ip suffix to preserve one trailing space")
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
		"103.45.130.141",
		"103.45.130.141",
		"475862105",
		false,
		true,
		true,
		true,
		"083",
		true,
	)

	assertTailDecodesToPlaintext(t, "103.45.130.141", extractTailFromFixedText(t, got))
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

	assertTailDecodesToPlaintext(t, "183.141.76.29", extractTailFromFixedText(t, got))
}

func TestFormatFixedText095MatchesCompatibilityShape(t *testing.T) {
	addedAt := time.Date(2025, 2, 23, 23, 43, 29, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"085二区-自定义属性-伤害统计-发面板包",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"43.230.72.10",
		"43.230.72.10",
		"390741416",
		true,
		true,
		true,
		true,
		"095",
		true,
	)

	wantPrefix := "085二区-自定义属性-伤害统计-发面板包|2025/2/23 23:43:29|2135/1/1 10:46:14|demo disclaimer|43.230.72.10|1|1|1|43.230.72.10 |1|390741416|"
	if !strings.HasPrefix(got, wantPrefix) {
		t.Fatalf("unexpected 095 fixed text prefix:\nwant prefix: %s\ngot:         %s", wantPrefix, got)
	}
	assertTailDecodesToPlaintext(t, "43.230.72.10", extractTailFromFixedText(t, got))
}

func TestBuildVersionTailWithoutFixedPrefixUsesRandomPrefix(t *testing.T) {
	got := BuildVersionTail("43.241.19.74", "")
	assertTailDecodesToPlaintext(t, "43.241.19.74", got)
}

func TestFormatFixedTextUsesBoundIPForFirstSlotAndServerIPForSecondSlot(t *testing.T) {
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
		t.Fatalf("expected fixed text to use bound ip first and server ip second, got: %s", got)
	}
}

func TestFormatFixedTextUsesBoundIPWhenServerIPEmpty(t *testing.T) {
	addedAt := time.Date(2023, 12, 17, 22, 1, 59, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"product-demo",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"183.141.76.29",
		"",
		"475862105",
		false,
		true,
		true,
		true,
		"099",
		false,
	)

	if !strings.Contains(got, "|183.141.76.29|0|1|1|183.141.76.29|1|475862105") {
		t.Fatalf("expected fixed text to fall back to bound ip when server ip is empty, got: %s", got)
	}
}

func TestFormatFixedTextVersionTailUsesSecondIP(t *testing.T) {
	addedAt := time.Date(2025, 2, 23, 23, 43, 29, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"085二区-自定义属性-伤害统计-发面板包",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"43.241.19.74",
		"10.10.10.10",
		"390741416",
		true,
		true,
		true,
		true,
		"095",
		true,
	)

	wantPrefix := "085二区-自定义属性-伤害统计-发面板包|2025/2/23 23:43:29|2135/1/1 10:46:14|demo disclaimer|43.241.19.74|1|1|1|10.10.10.10 |1|390741416|"
	if !strings.HasPrefix(got, wantPrefix) {
		t.Fatalf("unexpected 095 fixed text prefix with split ips:\nwant prefix: %s\ngot:         %s", wantPrefix, got)
	}
	assertTailDecodesToPlaintext(t, "10.10.10.10", extractTailFromFixedText(t, got))
}

func TestFormatFixedTextVersionTailFallsBackToBoundIPWhenSecondIPEmpty(t *testing.T) {
	addedAt := time.Date(2025, 2, 23, 23, 43, 29, 0, time.Local)
	expiresAt := time.Date(2135, 1, 1, 10, 46, 14, 0, time.Local)

	got := FormatFixedText(
		"085二区-自定义属性-伤害统计-发面板包",
		addedAt,
		expiresAt,
		"demo disclaimer",
		"43.241.19.74",
		"",
		"390741416",
		true,
		true,
		true,
		true,
		"095",
		true,
	)

	assertTailDecodesToPlaintext(t, "43.241.19.74", extractTailFromFixedText(t, got))
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
