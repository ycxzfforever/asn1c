#include <stdio.h>
#include <assert.h>
#include <math.h>

#define	EMIT_ASN_DEBUG	1
#include <REAL.h>

static char reconstructed[2][512];
static int reconstr_lens[2];

static int
callback(const void *buffer, size_t size, void *app_key) {
	char *buf = reconstructed[app_key ? 1 : 0];
	int *len = &reconstr_lens[app_key ? 1 : 0];

	if(*len + size >= sizeof(reconstructed[0]))
		return -1;

	memcpy(buf + *len, buffer, size);
	*len += size;

	return 0;
}

static char *
d2s(double d, int canonical, const char *str) {
	ssize_t s;

	reconstr_lens[canonical] = 0;
	s = REAL__dump(d, canonical, callback, (void *)(ptrdiff_t)canonical);
	assert(s < sizeof(reconstructed[canonical]));
	assert(s == reconstr_lens[canonical]);
	reconstructed[canonical][s] = '\0';	// ASCIIZ
	return reconstructed[canonical];
}

/*
 * Verify that a string representation of a given floating point value
 * is as given in the (sample) and (canonical_sample) arguments.
 */
static void
check_str_representation(double d, const char *sample, const char *canonical_sample) {
	char *s0, *s1;

	s0 = d2s(d, 0, sample);
	s1 = d2s(d, 1, canonical_sample);

	if(sample) {
		printf("Checking %f->[\"%s\"] against [\"%s\"]%s\n",
			d, s0, sample,
			canonical_sample ? " (canonical follows...)" : ""
		);
		assert(!strcmp(s0, sample));
	}
	if(canonical_sample) {
		printf("Checking %f->[\"%s\"] against [\"%s\"] (canonical)\n",
			d, s1, canonical_sample);
		assert(!strcmp(s1, canonical_sample));
	}
}

#define	check(rn, d, str1, str2)	\
	check_impl(rn, d, str1, str2, __LINE__)

static void
check_impl(REAL_t *rn, double orig_dbl, const char *sample, const char *canonical_sample, int line) {
	double val;
	uint8_t *p, *end;
	int ret;

	printf("Line %d: double value %.12f [", line, orig_dbl);
	for(p = (uint8_t *)&orig_dbl, end = p + sizeof(double); p < end ; p++)
		printf("%02x", *p);
	printf("] (ilogb %d)\n", ilogb(orig_dbl));

	val = frexp(orig_dbl, &ret);
	printf("frexp(%f, %d): [", val, ret);
	for(p = (uint8_t *)&val, end = p + sizeof(double); p < end ; p++)
		printf("%02x", *p);
	printf("]\n");

	ret = asn_double2REAL(rn, orig_dbl);
	assert(ret == 0);

	printf("converted into [");
	for(p = rn->buf, end = p + rn->size; p < end; p++)
		printf("%02x", *p);
	printf("]: %d\n", rn->size);

	ret = asn_REAL2double(rn, &val);
	assert(ret == 0);

	printf("and back to double: [");
	for(p = (uint8_t *)&val, end = p + sizeof(double); p < end ; p++)
		printf("%02x", *p);
	printf("] (ilogb %d)\n", ilogb(val));

	printf("%.12f vs %.12f\n", val, orig_dbl);
	assert((isnan(orig_dbl) && isnan(val)) || val == orig_dbl);
	printf("OK\n");

	check_str_representation(val, sample, canonical_sample);
}
static void
check_xer(int fuzzy, double orig_value) {
	asn_enc_rval_t er;
	asn_dec_rval_t rc;
	REAL_t st;
	REAL_t *newst0 = 0;
	REAL_t *newst1 = 0;
	REAL_t **newst0p = &newst0;
	REAL_t **newst1p = &newst1;
	double value0, value1;
	int ret;

	memset(&st, 0, sizeof(st));
	ret = asn_double2REAL(&st, orig_value);
	assert(ret == 0);

	reconstr_lens[0] = 0;
	reconstr_lens[1] = 0;
	er = xer_encode(&asn_DEF_REAL, &st,
		XER_F_BASIC, callback, 0);
	assert(er.encoded == reconstr_lens[0]);
	er = xer_encode(&asn_DEF_REAL, &st,
		XER_F_CANONICAL, callback, (void *)1);
	assert(er.encoded == reconstr_lens[1]);
	reconstructed[0][reconstr_lens[0]] = 0;
	reconstructed[1][reconstr_lens[1]] = 0;

	printf("%f vs (%d)[%s] & (%d)%s",
		orig_value,
		reconstr_lens[1], reconstructed[1],
		reconstr_lens[0], reconstructed[0]
	);

	rc = xer_decode(0, &asn_DEF_REAL, (void **)newst0p,
		reconstructed[0], reconstr_lens[0]);
	assert(rc.code == RC_OK);
	assert(rc.consumed < reconstr_lens[0]);

	rc = xer_decode(0, &asn_DEF_REAL, (void **)newst1p,
		reconstructed[1], reconstr_lens[1]);
	assert(rc.code == RC_OK);
	assert(rc.consumed == reconstr_lens[1]);

	ret = asn_REAL2double(newst0, &value0);
	assert(ret == 0);
	ret = asn_REAL2double(newst1, &value1);
	assert(ret == 0);

	assert((isnan(value0) && isnan(orig_value))
		|| value0 == orig_value
		|| fuzzy);
	assert((isnan(value1) && isnan(orig_value))
		|| value1 == orig_value);

	assert(newst0->size == st.size || fuzzy);
	assert(newst1->size == st.size);
	assert(fuzzy || memcmp(newst0->buf, st.buf, st.size) == 0);
	assert(memcmp(newst1->buf, st.buf, st.size) == 0);
}

static void
check_ber_buffer_twoway(double d, const char *sample, const char *canonical_sample, uint8_t *inbuf, size_t insize, uint8_t *outbuf, size_t outsize) {
	REAL_t rn;
	double val;
	int ret;

	/*
	 * Decode our expected buffer and check that it matches the given (d).
	 */
	rn.buf = inbuf;
	rn.size = insize;
	asn_REAL2double(&rn, &val);
	if(isnan(val)) assert(isnan(d));
	if(isnan(d)) assert(isnan(val));
	if(!isnan(val) && !isnan(d)) {
        assert(copysign(1.0, d) == copysign(1.0, val));
        assert(d == val);
    }

	/*
	 * Encode value and check that it matches our expected buffer.
	 */
	memset(&rn, 0, sizeof(rn));
	ret = asn_double2REAL(&rn, d);
	assert(ret == 0);
    uint8_t *p, *end;
	printf("received as:   [");
	for(p = rn.buf, end = p + rn.size; p < end; p++)
		printf("%02x", *p);
	printf("]\n");
	printf("received as:   [");
	for(p = outbuf, end = p + outsize; p < end; p++)
		printf("%02x", *p);
	printf("]\n");
	if(rn.size != outsize) {
		printf("Encoded %f into %d expected %ld\n",
			d, (int)rn.size, outsize);
		assert(rn.size == outsize);
	}
	assert(memcmp(rn.buf, outbuf, rn.size) == 0);

	check_str_representation(d, sample, canonical_sample);
}

static void
check_ber_buffer_oneway(double d, const char *sample, const char *canonical_sample, uint8_t *buf, size_t bufsize) {
	REAL_t rn;
	double val;
	uint8_t *p, *end;
	int ret;

	memset(&rn, 0, sizeof(rn));

	printf("verify double value %.12f [", d);
	for(p = (uint8_t *)&d, end = p + sizeof(double); p < end ; p++)
		printf("%02x", *p);
	printf("] (ilogb %d)\n", ilogb(d));


	ret = asn_double2REAL(&rn, d);
	assert(ret == 0);

	printf("canonical DER: [");
	for(p = rn.buf, end = p + rn.size; p < end; p++)
		printf("%02x", *p);
	printf("]\n");

	rn.buf = buf;
	rn.size = bufsize;

	printf("received as:   [");
	for(p = rn.buf, end = p + rn.size; p < end; p++)
		printf("%02x", *p);
	printf("]\n");

	ret = asn_REAL2double(&rn, &val);
	assert(ret == 0);

	printf("%.12f vs %.12f\n", d, val);

	assert(val == d);

	check_str_representation(val, sample, canonical_sample);
}


static void
check_ber_encoding() {
	static const double zero = 0.0;

#define CHECK_BER_STRICT(v, nocan, can, inbuf, outbuf)	\
	check_ber_buffer_twoway(v, nocan, can, inbuf, sizeof(inbuf), outbuf, sizeof(outbuf))

#define CHECK_BER_NONSTRICT(v, nocan, can, buf)	\
	check_ber_buffer_oneway(v, nocan, can, buf, sizeof(buf))

	/*
	 * X.690 8.4 Encoding of an enumerated value.
	 */

	/* 8.5.2 If the real value is the value plus zero,
	 * there shall be no contents octet in the encoding */
	{ uint8_t b_0[] = {};
	  CHECK_BER_STRICT(0, "0", "0", b_0, b_0);
    }

	/* 8.5.3 When -0 is to be encoded, there shall be only one contents octet */
	{ uint8_t b_m0[] = { 0x43 };
	  CHECK_BER_STRICT(-0.0, "-0", "-0", b_m0, b_m0);
    }

    /* Old way of encoding -0.0: 8.5.6 a) */
	{ uint8_t b_m0[] = { 0x43 };
	  uint8_t b_m0_856a[]   = { 0xC0, 0x00 };  /* #8.5.6 a) */
	  uint8_t b_m0_856a_1[] = { 0xC0, 0x00, 0x00 };
	  uint8_t b_m0_856a_2[] = { 0xC0, 0x00, 0x00, 0x00 };
	  uint8_t b_m0_856a_3[] = { 0xC0, 0x00, 0x00, 0x00, 0x00 };
	  CHECK_BER_STRICT(-0.0, "-0", "-0", b_m0_856a, b_m0);
	  CHECK_BER_STRICT(-0.0, "-0", "-0", b_m0_856a_1, b_m0);
	  CHECK_BER_STRICT(-0.0, "-0", "-0", b_m0_856a_2, b_m0);
	  CHECK_BER_STRICT(-0.0, "-0", "-0", b_m0_856a_3, b_m0);
    }

	/* 8.5.6 c) => 8.5.9 SpecialRealValue */
	{ uint8_t b_pinf[] = { 0x40 };
	  uint8_t b_minf[] = { 0x41 };
	  uint8_t b_nan[]  = { 0x42 };
	  CHECK_BER_STRICT(1.0/zero, "<PLUS-INFINITY/>", "<PLUS-INFINITY/>", b_pinf, b_pinf);
	  CHECK_BER_STRICT(-1.0/zero, "<MINUS-INFINITY/>", "<MINUS-INFINITY/>", b_minf, b_minf);
	  CHECK_BER_STRICT(zero/zero, "<NOT-A-NUMBER/>", "<NOT-A-NUMBER/>", b_nan, b_nan);
    }

	{
	uint8_t b_1_0[] =
		{ 0x80, 0xcc, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	uint8_t b_1_1[] =
		{ 0x80, 0xcc, 0x11, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a };
	uint8_t b_3_14[] =
		{ 0x80, 0xcd, 0x19, 0x1e, 0xb8, 0x51, 0xeb, 0x85, 0x1f };
	uint8_t b_3_14_mo1[] =
		{ 0xC0, 0xc5, 0x19, 0x1e, 0xb8, 0x51, 0xeb, 0x85, 0x1f,3};
	uint8_t b_3_14_mo2[] =
		{ 0x80, 0xbd, 0x19, 0x1e, 0xb8, 0x51, 0xeb, 0x85, 0x1f,3,2};

	CHECK_BER_NONSTRICT(1.0, "1.0", "1.0E0", b_1_0);
	CHECK_BER_NONSTRICT(1.1, "1.1", "1.1E0", b_1_1);
	CHECK_BER_NONSTRICT(3.14, "3.14", "3.14E0", b_3_14);
	/* These two are very interesting! They check mantissa overflow! */
	CHECK_BER_NONSTRICT(-3.14, "-3.14", "-3.14E0", b_3_14_mo1);
	CHECK_BER_NONSTRICT(3.14, "3.14", "3.14E0", b_3_14_mo2);
	}
}

int
main() {
	REAL_t rn;
	static const double zero = 0.0;
	memset(&rn, 0, sizeof(rn));

	check_ber_encoding();

	check(&rn, 0.0, "0", "0");
	check(&rn, -0.0, "-0", "-0");	/* minus-zero */
	check(&rn, zero/zero, "<NOT-A-NUMBER/>", "<NOT-A-NUMBER/>");
	check(&rn, 1.0/zero, "<PLUS-INFINITY/>", "<PLUS-INFINITY/>");
	check(&rn, -1.0/zero, "<MINUS-INFINITY/>", "<MINUS-INFINITY/>");
	check(&rn, 1.0, "1.0", "1.0E0");
	check(&rn, -1.0, "-1.0", "-1.0E0");
	check(&rn, 0.1, "0.1", "1.0E-1");
	check(&rn, 0.01, "0.01", "1.0E-2");
	check(&rn, 0.02, "0.02", "2.0E-2");
	check(&rn, 0.09, "0.09", "9.0E-2");
	check(&rn, 1.5, "1.5", "1.5E0");
	check(&rn, 0.33333, "0.33333", "3.3333E-1");
	check(&rn, 2, "2.0", "2.0E0");
	check(&rn, 2.1, "2.1", "2.1E0");
	check(&rn, 3, "3.0", "3.0E0");
	check(&rn, 3.1, "3.1", "3.1E0");
	check(&rn, 3.14, "3.14", "3.14E0");
	check(&rn, 3.1415, "3.1415", "3.1415E0");
	check(&rn, 3.141592, "3.141592", "3.141592E0");
	check(&rn, 3.14159265, "3.14159265", "3.14159265E0");
	check(&rn, -3.14159265, "-3.14159265", "-3.14159265E0");
	check(&rn, 14159265.0, "14159265.0", "1.4159265E7");
	check(&rn, -123456789123456789.0, "-123456789123456784.0", "-1.234567891234568E17");
	check(&rn, 0.00000000001, "0.00000000001", "9.999999999999999E-12");
	check(&rn, 0.00000000002, "0.00000000002", "2.0E-11");
	check(&rn, 0.00000000009, "0.00000000009", "9.0E-11");
	check(&rn, 0.000000000002, "0.000000000002", "2.0E-12");
	check(&rn, 0.0000000000002, "0.0000000000002", "2.0E-13");
	check(&rn, 0.00000000000002, "0.00000000000002", "2.0E-14");
	check(&rn, 0.000000000000002, "0.000000000000002", "2.0E-15");
	check(&rn, 0.0000000000000002, "0.0", "2.0E-16");
	check(&rn, 0.0000000000000000000001, "0.0", "1.0E-22");
	check(&rn, 0.000000000000000000000000000001, "0.0", "1.0E-30"); /* proved 2B a problem */
	check(&rn,-0.000000000000000000000000000001, "-0.0", "-1.0E-30"); /* proved 2B a problem */
	check(&rn, 0.0000000000010000000001000000000001, 0, 0);
	check(&rn, 0.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001, 0, 0);
	check(&rn, 0.000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001, 0, 0);
	check(&rn,-0.000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001, 0, 0);
	check(&rn,-3.33333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333, 0, 0);
	check(&rn, 0.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000033333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333, 0, 0);
	check(&rn, -0.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001, 0, 0);


#ifdef	NAN
	check_xer(0, NAN);	/* "<NOT-A-NUMBER/>" */
#else
	check_xer(0, zero/zero);	/* "<NOT-A-NUMBER/>" */
#endif
#ifdef	INFINITY
	check_xer(0, INFINITY);		/* "<PLUS-INFINITY/>" */
	check_xer(0, -INFINITY);	/* "<MINUS-INFINITY/>" */
#else
	check_xer(0, 1.0/zero);		/* "<PLUS-INFINITY/>" */
	check_xer(0, -1.0/zero);	/* "<MINUS-INFINITY/>" */
#endif
	check_xer(0, 1.0);
	check_xer(0, -1.0);
	check_xer(0, 1.5);
	check_xer(0, 123);
	check_xer(1, 0.0000000000000000000001);
	check_xer(1, -0.0000000000000000000001);

	return 0;
}
