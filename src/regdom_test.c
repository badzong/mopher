/* Generated file. See Makefile.dist for details */
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

// NULL input.
{NULL, NULL, 0},
// Mixed case.
{"COM", NULL, 0},
{"example.COM", "example.com", 0},
{"WwW.example.COM", "example.com", 0},
// Leading dot.
{".com", NULL, 0},
{".example", NULL, 0},
{".example.com", NULL, 0},
{".example.example", NULL, 0},
// Unlisted TLD.
{"example", NULL, 0},
{"example.example", "example.example", 0},
{"b.example.example", "example.example", 0},
{"a.b.example.example", "example.example", 0},
// Listed, but non-Internet, TLD.
//{"local", NULL, 0},
//{"example.local", NULL, 0},
//{"b.example.local", NULL, 0},
//{"a.b.example.local", NULL, 0},
// TLD with only 1 rule.
{"biz", NULL, 0},
{"domain.biz", "domain.biz", 0},
{"b.domain.biz", "domain.biz", 0},
{"a.b.domain.biz", "domain.biz", 0},
// TLD with some 2-level rules.
{"com", NULL, 0},
{"example.com", "example.com", 0},
{"b.example.com", "example.com", 0},
{"a.b.example.com", "example.com", 0},
{"uk.com", NULL, 0},
{"example.uk.com", "example.uk.com", 0},
{"b.example.uk.com", "example.uk.com", 0},
{"a.b.example.uk.com", "example.uk.com", 0},
{"test.ac", "test.ac", 0},
// TLD with only 1 (wildcard) rule.
{"cy", NULL, 0},
{"c.cy", NULL, 0},
{"b.c.cy", "b.c.cy", 0},
{"a.b.c.cy", "b.c.cy", 0},
// More complex TLD.
{"jp", NULL, 0},
{"test.jp", "test.jp", 0},
{"www.test.jp", "test.jp", 0},
{"ac.jp", NULL, 0},
{"test.ac.jp", "test.ac.jp", 0},
{"www.test.ac.jp", "test.ac.jp", 0},
{"kyoto.jp", NULL, 0},
{"test.kyoto.jp", "test.kyoto.jp", 0},
{"ide.kyoto.jp", NULL, 0},
{"b.ide.kyoto.jp", "b.ide.kyoto.jp", 0},
{"a.b.ide.kyoto.jp", "b.ide.kyoto.jp", 0},
{"c.kobe.jp", NULL, 0},
{"b.c.kobe.jp", "b.c.kobe.jp", 0},
{"a.b.c.kobe.jp", "b.c.kobe.jp", 0},
{"city.kobe.jp", "city.kobe.jp", 0},
{"www.city.kobe.jp", "city.kobe.jp", 0},
// TLD with a wildcard rule and exceptions.
{"ck", NULL, 0},
{"test.ck", NULL, 0},
{"b.test.ck", "b.test.ck", 0},
{"a.b.test.ck", "b.test.ck", 0},
{"www.ck", "www.ck", 0},
{"www.www.ck", "www.ck", 0},
// US K12.
{"us", NULL, 0},
{"test.us", "test.us", 0},
{"www.test.us", "test.us", 0},
{"ak.us", NULL, 0},
{"test.ak.us", "test.ak.us", 0},
{"www.test.ak.us", "test.ak.us", 0},
{"k12.ak.us", NULL, 0},
{"test.k12.ak.us", "test.k12.ak.us", 0},
{"www.test.k12.ak.us", "test.k12.ak.us", 0},
// IDN labels.
{"食狮.com.cn", "食狮.com.cn", 0},
{"食狮.公司.cn", "食狮.公司.cn", 0},
{"www.食狮.公司.cn", "食狮.公司.cn", 0},
{"shishi.公司.cn", "shishi.公司.cn", 0},
{"公司.cn", NULL, 0},
{"食狮.中国", "食狮.中国", 0},
{"www.食狮.中国", "食狮.中国", 0},
{"shishi.中国", "shishi.中国", 0},
{"中国", NULL, 0},
// Same as above, but punycoded.
{"xn--85x722f.com.cn", "xn--85x722f.com.cn", 0},
{"xn--85x722f.xn--55qx5d.cn", "xn--85x722f.xn--55qx5d.cn", 0},
{"www.xn--85x722f.xn--55qx5d.cn", "xn--85x722f.xn--55qx5d.cn", 0},
{"shishi.xn--55qx5d.cn", "shishi.xn--55qx5d.cn", 0},
{"xn--55qx5d.cn", NULL, 0},
{"xn--85x722f.xn--fiqs8s", "xn--85x722f.xn--fiqs8s", 0},
{"www.xn--85x722f.xn--fiqs8s", "xn--85x722f.xn--fiqs8s", 0},
{"shishi.xn--fiqs8s", "shishi.xn--fiqs8s", 0},
{"xn--fiqs8s", NULL, 0},
