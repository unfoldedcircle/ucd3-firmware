#include <gtest/gtest.h>
#include "improv.h"

TEST(ImprovWifiTest, BuildRpcResponse_Correctness) {
    const char* data_str = "123";
    const char* datum[] = {data_str};
    uint16_t buf_len = 0;
    
    // Command is arbitrary, e.g. WIFI_SETTINGS
    uint8_t* out = build_rpc_response(WIFI_SETTINGS, datum, 1, true, &buf_len);
    
    ASSERT_TRUE(out != nullptr);
    ASSERT_GT(buf_len, 0);
    
    // Expected structure:
    // out[0]: Command
    // out[1]: Length (Data length)
    // out[2]: Str len (3)
    // out[3..5]: "123"
    // out[6]: Checksum
    
    // Calculate expected length
    // 1 (cmd) + 1 (len byte) + 1 (datum len byte) + 3 (datum) + 1 (checksum) = 7
    EXPECT_EQ(buf_len, 7);
    
    // Check Command
    EXPECT_EQ(out[0], WIFI_SETTINGS);
    
    // Check Length Byte (The Bug 1: this is currently 0)
    // Data length = 1 (datum len byte) + 3 (datum) = 4
    EXPECT_EQ(out[1], 4); 
    
    // Check Datum Length
    EXPECT_EQ(out[2], 3);
    
    // Check Datum
    EXPECT_EQ(out[3], '1');
    EXPECT_EQ(out[4], '2');
    EXPECT_EQ(out[5], '3');
    
    // Check Checksum (The Bug 2: loop variable wrong)
    uint32_t calc_sum = 0;
    for(int i=0; i<6; i++) {
        calc_sum += out[i];
    }
    EXPECT_EQ(out[6], (uint8_t)calc_sum);
    
    free(out);
}
