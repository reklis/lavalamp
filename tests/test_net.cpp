#include "metrics/net.h"

#include "test_harness.h"

// Verbatim /proc/net/dev from the target machine: two header lines and
// fifteen interfaces, only two of which should count. Captured deliberately
// rather than simplified -- `lo` alone carries 91 GB each way here, and a
// parser that sums blindly produces a lamp driven by loopback.
static constexpr std::string_view kNetDev =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo: 91572712926 25758545    0    0    0     0          0         0 91572712926 25758545    0    0    0     0       0          0\n"
    "wlp99s0: 711873170029 559766855    0    1    0     0          0         0 86430704077 291320545    0   68    0     0       0          0\n"
    "tailscale0:      86       1    0    0    0     0          0         0    33756     635    0    0    0     0       0          0\n"
    "br-fa87cc1d52f9:       0       0    0    0    0     0          0         0        0       0    0   29    0     0       0          0\n"
    "docker0: 1461400936 19187478    0    0    0     0          0         0 56247045847 27192294    0    2    0     0       0          0\n"
    "br-fa5db6fe7c62:       0       0    0    0    0     0          0         0        0       0    0   29    0     0       0          0\n"
    "veth7b64bcd: 310615744 2832991    0    0    0     0          0         0 9090921978 4329128    0    0    0     0       0          0\n"
    "veth921d1b2: 18909308    2162    0    0    0     0          0         0 13896937   73926    0    0    0     0       0          0\n"
    "br-349bfb9a64e1: 5710072069 78433880    0    0    0     0          0         0 205322038127 81005177    0    9    0     0       0          0\n"
    "br-92b3d8789e55: 39793625  122741    0    0    0     0          0         0 41874588  171675    0    2    0     0       0          0\n"
    "veth846da1d: 1113427    1789    0    0    0     0          0         0  9067362   37731    0    0    0     0       0          0\n"
    "vethb86b332: 38805503  115862    0    0    0     0          0         0 38262005  163524    0    0    0     0       0          0\n"
    "veth21cc202:  392921    2348    0    0    0     0          0         0  7419688   38996    0    0    0     0       0          0\n"
    "vetha6ede13:  306137     171    0    0    0     0          0         0  6923659   35476    0    0    0     0       0          0\n"
    "  tun0: 670345877 1557866    0    0    0     0          0         0 1231092305 2100425    0 2664    0     0       0          0\n";

// wlp99s0 rx advanced by exactly 104857600 bytes (100 MiB).
static constexpr std::string_view kNetDevPlus100MB =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo: 91572712926 25758545    0    0    0     0          0         0 91572712926 25758545    0    0    0     0       0          0\n"
    "wlp99s0: 711978027629 559766855    0    1    0     0          0         0 86430704077 291320545    0   68    0     0       0          0\n"
    "  tun0: 670345877 1557866    0    0    0     0          0         0 1231092305 2100425    0 2664    0     0       0          0\n";

// tun0 gone: the whitelisted total goes DOWN, which must not read as traffic.
static constexpr std::string_view kNetDevNoTun =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo: 91572712926 25758545    0    0    0     0          0         0 91572712926 25758545    0    0    0     0       0          0\n"
    "wlp99s0: 711873170029 559766855    0    1    0     0          0         0 86430704077 291320545    0   68    0     0       0          0\n";

TEST(interface_whitelist) {
  CHECK(is_counted_interface("wlp99s0"));
  CHECK(is_counted_interface("eth0"));
  CHECK(is_counted_interface("enp5s0"));
  CHECK(is_counted_interface("tun0"));
  CHECK(!is_counted_interface("lo"));
  CHECK(!is_counted_interface("docker0"));
  CHECK(!is_counted_interface("br-fa87cc1d52f9"));
  CHECK(!is_counted_interface("veth7b64bcd"));
  CHECK(!is_counted_interface("tailscale0"));
}

TEST(sums_only_counted_interfaces) {
  // wlp99s0 (711873170029 + 86430704077) + tun0 (670345877 + 1231092305)
  // lo alone is 91572712926 each way and must NOT appear
  CHECK_EQ(parse_proc_net_dev(kNetDev).value(), 800205312288ull);
}

TEST(skips_two_header_lines_and_malformed_rows) {
  CHECK(!parse_proc_net_dev("").has_value());
  CHECK(!parse_proc_net_dev("Inter-|   Receive\n face |bytes\n").has_value());
  CHECK(!parse_proc_net_dev("garbage without colons\n").has_value());
}

TEST(first_sample_is_unavailable) {  // Review Focus 1
  NetSource src;
  CHECK(!src.sample(kNetDev, 1.0).has_value());
}

TEST(idle_traffic_normalizes_near_zero) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  auto v = src.sample(kNetDev, 1.0);  // zero bytes moved
  CHECK(v.has_value());
  CHECK_NEAR(*v, 0.0, 1e-6);
}

TEST(burst_then_idle_falls_back_toward_zero) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  double burst = *src.sample(kNetDevPlus100MB, 1.0);
  double after = *src.sample(kNetDevPlus100MB, 1.0);
  CHECK(burst > 0.5);   // a 100 MB/s burst must read high
  CHECK(after < burst); // and then relax
}

TEST(interface_disappearing_clamps_to_zero) {  // Review Focus 2
  NetSource src;
  src.sample(kNetDev, 1.0);
  auto v = src.sample(kNetDevNoTun, 1.0);  // tun0 gone, total went DOWN
  CHECK(v.has_value());
  CHECK(*v >= 0.0);
}

TEST(zero_or_negative_dt_is_unavailable) {
  NetSource src;
  src.sample(kNetDev, 1.0);
  CHECK(!src.sample(kNetDevPlus100MB, 0.0).has_value());
}
