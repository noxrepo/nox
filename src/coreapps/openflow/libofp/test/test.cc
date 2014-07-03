#include "ofp.hh"
#include "ofp-print.h"

int main()
{
	struct ofpbuf *hello = ofputil_encode_hello(0x10);  
	ofp_print(stdout, hello->data, hello->size, 1);
	ofpbuf_delete(hello);
	return 0;
}
