/* Code that must live in banked memory, and enough of it that the
   linker has to generate more than one bank instance for the pair. */
#pragma clang section text="bankedcode"

#define BULK(n) int t##n(int x){return x*n+(x>>1)+(x^n)+(x<<2)-(x/3);}
#define BULK10(n) BULK(n##0) BULK(n##1) BULK(n##2) BULK(n##3) BULK(n##4) \
                  BULK(n##5) BULK(n##6) BULK(n##7) BULK(n##8) BULK(n##9)
#define BULK100(n) BULK10(n##0) BULK10(n##1) BULK10(n##2) BULK10(n##3) \
                   BULK10(n##4) BULK10(n##5) BULK10(n##6) BULK10(n##7) \
                   BULK10(n##8) BULK10(n##9)
BULK100(1)
BULK100(2)

/* Two entry points, each reaching every function in its own half, so
   nothing is garbage-collected and both halves are really retained. */
int far_a(int x) { return t100(x) + t101(x) + t102(x) + t103(x) + t104(x) + t105(x) + t106(x) + t107(x) + t108(x) + t109(x) + t110(x) + t111(x) + t112(x) + t113(x) + t114(x) + t115(x) + t116(x) + t117(x) + t118(x) + t119(x) + t120(x) + t121(x) + t122(x) + t123(x) + t124(x) + t125(x) + t126(x) + t127(x) + t128(x) + t129(x) + t130(x) + t131(x) + t132(x) + t133(x) + t134(x) + t135(x) + t136(x) + t137(x) + t138(x) + t139(x) + t140(x) + t141(x) + t142(x) + t143(x) + t144(x) + t145(x) + t146(x) + t147(x) + t148(x) + t149(x) + t150(x) + t151(x) + t152(x) + t153(x) + t154(x) + t155(x) + t156(x) + t157(x) + t158(x) + t159(x) + t160(x) + t161(x) + t162(x) + t163(x) + t164(x) + t165(x) + t166(x) + t167(x) + t168(x) + t169(x) + t170(x) + t171(x) + t172(x) + t173(x) + t174(x) + t175(x) + t176(x) + t177(x) + t178(x) + t179(x) + t180(x) + t181(x) + t182(x) + t183(x) + t184(x) + t185(x) + t186(x) + t187(x) + t188(x) + t189(x) + t190(x) + t191(x) + t192(x) + t193(x) + t194(x) + t195(x) + t196(x) + t197(x) + t198(x) + t199(x); }
int far_b(int x) { return t200(x) + t201(x) + t202(x) + t203(x) + t204(x) + t205(x) + t206(x) + t207(x) + t208(x) + t209(x) + t210(x) + t211(x) + t212(x) + t213(x) + t214(x) + t215(x) + t216(x) + t217(x) + t218(x) + t219(x) + t220(x) + t221(x) + t222(x) + t223(x) + t224(x) + t225(x) + t226(x) + t227(x) + t228(x) + t229(x) + t230(x) + t231(x) + t232(x) + t233(x) + t234(x) + t235(x) + t236(x) + t237(x) + t238(x) + t239(x) + t240(x) + t241(x) + t242(x) + t243(x) + t244(x) + t245(x) + t246(x) + t247(x) + t248(x) + t249(x) + t250(x) + t251(x) + t252(x) + t253(x) + t254(x) + t255(x) + t256(x) + t257(x) + t258(x) + t259(x) + t260(x) + t261(x) + t262(x) + t263(x) + t264(x) + t265(x) + t266(x) + t267(x) + t268(x) + t269(x) + t270(x) + t271(x) + t272(x) + t273(x) + t274(x) + t275(x) + t276(x) + t277(x) + t278(x) + t279(x) + t280(x) + t281(x) + t282(x) + t283(x) + t284(x) + t285(x) + t286(x) + t287(x) + t288(x) + t289(x) + t290(x) + t291(x) + t292(x) + t293(x) + t294(x) + t295(x) + t296(x) + t297(x) + t298(x) + t299(x); }
