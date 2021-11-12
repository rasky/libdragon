
#include "overlay0/md5.h"
#include "overlay1/sha1.h"

void test_overlays(TestContext *ctx) {
	char *buf = "Hello, world\n";
	int bufsize = strlen(buf);

	ASSERT_EQUAL_UNSIGNED((uint32_t)md5_init >> 24, 0xE0, "md5_init is not in overlay 0");
	ASSERT_EQUAL_UNSIGNED((uint32_t)sha1_init >> 24, 0xE1, "sha1_init is not in overlay 1");

	MD5_CTX md5; SHA1_CTX sha1;
	uint8_t md5_hash[16], sha1_hash[20]; 

	md5_init(&md5);
	sha1_init(&sha1);
	md5_update(&md5, (uint8_t*)buf, bufsize);
	sha1_update(&sha1, (uint8_t*)buf, bufsize);
	md5_final(&md5, md5_hash);
	sha1_final(&sha1, sha1_hash);
}
