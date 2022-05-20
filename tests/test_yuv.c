#include "../include/rdp_commands.h"

#define DP_STATUS_BUSY                 (1<<6)

void rdp_attach_buffer(uint32_t *buffer, int width)
{
	#define RDP_TILE_FORMAT_RGBA  0
	#define RDP_TILE_SIZE_32BIT 3
    rdp_set_color_image(PhysicalAddr(buffer), 
		RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_32BIT, width - 1);
}

void rdp_wait(void)
{
    volatile uint32_t *DP_STATUS = (volatile uint32_t*)0xA410000C;
    RSP_WAIT_LOOP(500) {
    	if (!(*DP_STATUS & DP_STATUS_BUSY))
    		break;
    	
    }
}

void yuv_test_wait(void)
{
	rdp_sync_full();  // enqueue a SYNC_FULL to make sure RDP will write everything
	rspq_sync();      // wait for the end of RSP queue
	rdp_wait();	      // wait for RDP to be idle
}

#define yuv_test_init(ys, us, vs, fs) \
	rdp_init(); DEFER(rdp_close()); \
	yuv_init(); DEFER(rspq_close(); yuv_close()); \
	uint8_t *y = malloc_uncached(ys*ys); DEFER(free_uncached(y)); \
	uint8_t *u = malloc_uncached(us*us); DEFER(free_uncached(u)); \
	uint8_t *v = malloc_uncached(vs*vs); DEFER(free_uncached(v)); \
	uint32_t *fb = malloc_uncached(fs*fs*4); DEFER(free_uncached(fb)); \
	rdp_attach_buffer(fb, fs);

void test_yuv_3p(TestContext *ctx)
{
	yuv_test_init(32, 16, 16, 64);

	memset(y, 0x66, 32*32);
	memset(u, 0xAC, 16*16);
	memset(v, 0x23, 16*16);
	memset(fb, 0xEE, 64*64*4);

	yuv_config_t cfg = {
		.width = 32,     .height = 32,
		.out_width = 64, .out_height = 64,
		.zoom = YUV_ZOOM_NONE,
	};
	yuv_draw_frame_3p(&cfg, y, u, v);

	yuv_test_wait();

	for (int y=0;y<64;y++) {
		for (int x=0;x<64;x++) {
			uint32_t exp = (y >= 16 && y < 48 && x >= 16 && x < 48) ? 0x9fbde0 : 0;
			ASSERT_EQUAL_HEX(fb[y*64+x], exp,
				"invalid output pixel at (%d,%d)", x, y);
		}
	}
}

void test_yuv_align(TestContext *ctx)
{
	yuv_test_init(32, 16, 16, 128);
	memset(y, 0x66, 32*32);
	memset(u, 0xAC, 16*16);
	memset(v, 0x23, 16*16);

	#define BKG  0x00000000    // background pixel
	#define FRG  0x009fbde0    // yuv converted pixel

	int hotpoints[9][2] = { 
		{64,64},  {0,64},  {127,64},
		{64,0},   {0,0},   {127,0},
		{64,127}, {0,127}, {127,127}
	};

	for (int va=0;va<3;va++) {
		for (int ha=0;ha<3;ha++) {
			LOG("test %d,%d\n", ha, va);
			memset(fb, 0xEE, 128*128*4);

			yuv_config_t cfg = {				
				.width = 32, .height = 32,
				.halign = ha, .valign = va,
				.out_width = 128, .out_height = 128,
				.zoom = YUV_ZOOM_NONE,
			};
			yuv_draw_frame_3p(&cfg, y, u, v);
			yuv_test_wait();
			
			for (int j=0;j<9;j++) { 
				int x = hotpoints[j][0], y = hotpoints[j][1];
				LOG("hp (%d,%d) = %08lx\n", x, y, fb[y*128+x]);
			}

			int hotpoint_frg = va*3 + ha;
			for (int j=0;j<9;j++) { 
				int x = hotpoints[j][0], y = hotpoints[j][1];
				uint32_t exp = (j == hotpoint_frg) ? FRG : BKG;
				ASSERT_EQUAL_HEX(fb[y*128+x], exp,
					"invalid output pixel at (%d,%d)", x, y);
			}
		}
	}
}

int abs(int x) { return x < 0 ? -x : x; }

void test_yuv_colorspace(TestContext *ctx)
{
	yuv_test_init(32, 16, 16, 32);

	SRAND(0);
	for (int i=0;i<32*32;i++)
		y[i] = RANDN(256);
	for (int i=0;i<16*16;i++) {
		u[i] = RANDN(256);
		v[i] = RANDN(256);
	}

	// {
	// 	yuv_colorspace_t cs;
	// 	yuv_new_colorspace(&cs, 0.2126, 0.0722, 0, 256, 256);
	// 	debugf(".c0=%g, .c1=%g, .c2=%g, .c3=%g, .c4=%g, .y0=%d\n",
	// 		cs.c0, cs.c1, cs.c2, cs.c3, cs.c4, cs.y0);
	// 	debugf(".k0=%d, .k1=%d, .k2=%d, .k3=%d, .k4=%d, .k5=%d\n",
	// 		cs.k0, cs.k1, cs.k2, cs.k3, cs.k4, cs.k5);
	// }


	const yuv_colorspace_t* css[4] = {
		&YUV_BT601_TV, &YUV_BT601_FULL, &YUV_BT709_TV, &YUV_BT601_FULL
	};

	for (int csi=0; csi<4; csi++) {
		LOG("test %d\n", csi);

		yuv_config_t cfg; memset(&cfg, 0, sizeof(cfg));
		cfg.width = 32; cfg.height = 32;
		cfg.cs = css[csi];
		cfg.out_width = 32; cfg.out_height = 32;
		yuv_draw_frame_3p(&cfg, y, u, v);
		yuv_test_wait();

		for (int j=0;j<32;j++) {
			for (int i=0;i<32;i++) {
				uint8_t y0 = y[j*32+i], u0 = u[j/2*16+i/2], v0 = v[j/2*16+i/2];
				color_t cexp = yuv_to_rgb(y0, u0, v0, cfg.cs);
				uint32_t exp = (cexp.r << 24) | (cexp.g << 16) | (cexp.b << 8);
		        uint32_t fb0 = fb[j*32+i];

		        if (abs(((fb0>>24)&0xFF) - cexp.r) > 3 || 
		        	abs(((fb0>>16)&0xFF) - cexp.g) > 3 ||
		        	abs(((fb0>>8)&0xFF) - cexp.b) > 3)
		        	ASSERT_EQUAL_HEX(fb0, exp, "invalid colorspace conversion at (%d,%d) from [%d,%d,%d]", i, j, y0, u0, v0);
			}
		}
	}
}
