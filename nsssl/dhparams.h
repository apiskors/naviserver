static DH *get_dh512(void)
	{
	static unsigned char dh512_p[]={
		0xC3,0x88,0x01,0x3A,0xF1,0xF3,0xCB,0x8C,0x66,0xDF,0x6F,0x8D,
		0x62,0x6C,0x57,0xBA,0xF2,0xBF,0x38,0x6A,0x81,0xB1,0xE1,0xC7,
		0x94,0xDD,0x91,0x76,0xB6,0x8A,0x0C,0x94,0xAD,0x7C,0xF4,0x40,
		0x50,0xEF,0xE7,0x78,0xE9,0x47,0x49,0xC6,0xBF,0x07,0x79,0xDD,
		0x46,0xA4,0xCE,0x83,0x2D,0xCC,0x55,0x2B,0x40,0x03,0x64,0x41,
		0xCB,0x9C,0xEA,0xDB,
		};
	static unsigned char dh512_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh512_p,sizeof(dh512_p),NULL);
	dh->g=BN_bin2bn(dh512_g,sizeof(dh512_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
static DH *get_dh1024(void)
	{
	static unsigned char dh1024_p[]={
		0xE8,0x0D,0xF9,0x9E,0x76,0xD2,0xA9,0xCE,0x6C,0x62,0xE5,0x99,
		0xCC,0x8B,0x92,0xED,0xA1,0x98,0x3A,0xAA,0x40,0x60,0xCF,0xF8,
		0x17,0xAB,0x42,0x8A,0xD8,0xBA,0xC7,0xBD,0x39,0x8F,0xF0,0x88,
		0x8E,0x1F,0x03,0xD9,0x66,0x74,0xD3,0xC7,0xCB,0xC0,0x52,0x7B,
		0x6D,0x08,0x06,0x60,0x20,0xDE,0x90,0xDC,0x7B,0x7A,0x7C,0xE9,
		0x0A,0x18,0x0B,0xF5,0xBE,0x18,0x1E,0xE5,0x66,0x4A,0xB0,0xDC,
		0x86,0xBD,0x22,0x1C,0xF0,0x4F,0xCD,0xC3,0x44,0x4D,0xA5,0x9E,
		0x2A,0x37,0xB1,0x86,0xD1,0xD1,0x4E,0x32,0x0B,0xD6,0x68,0xFD,
		0x03,0x82,0x96,0x12,0x37,0x0C,0x03,0x82,0x26,0xC9,0x79,0xC5,
		0x0A,0x17,0xAA,0x76,0x60,0x2E,0xC0,0x06,0x6E,0x70,0x2C,0x71,
		0xBC,0x13,0x85,0xBA,0xAC,0x3B,0x8E,0x9B,
		};
	static unsigned char dh1024_g[]={
		0x02,
		};
	DH *dh;

	if ((dh=DH_new()) == NULL) return(NULL);
	dh->p=BN_bin2bn(dh1024_p,sizeof(dh1024_p),NULL);
	dh->g=BN_bin2bn(dh1024_g,sizeof(dh1024_g),NULL);
	if ((dh->p == NULL) || (dh->g == NULL))
		{ DH_free(dh); return(NULL); }
	return(dh);
	}
