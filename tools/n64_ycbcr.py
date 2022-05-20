#!/usr/bin/env python3
import sys

"""
N64_YCBCR - Calculate RDP coefficients for YCbCr conversion

This tool is useful to derive the RDP coefficients used to implement a specific
colorspace in the YCbCr -> RGB conversion. 

The yuv library (yuv.h) already supports 4 common colorspaces that were
calculated using this tool, so running this tool is only required to add
support for more colorspaces.
"""

# Explicitly error out on Python 2. The code can be run also on Python 2 but
# generates wrong results because of integer truncations. Let's avoid the
# pitfall...
if sys.version_info[0] < 3:
    raise RuntimeError("This requires Python3")

# 3x3 matrix inverse
def inv3(m):
    det = (m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) -
          m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]));
    invdet = 1.0 / det
    print("idet", invdet)
    return [
        [(m[1][1] * m[2][2] - m[2][1] * m[1][2]) * invdet,
        (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invdet,
        (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invdet],
        [(m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invdet,
        (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invdet,
        (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * invdet],
        [(m[1][0] * m[2][1] - m[2][0] * m[1][1]) * invdet,
        (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * invdet,
        (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * invdet]
    ]

def calc(kr, kb, y0, yrange, crange):
    kg = 1 - kr - kb

    # Inverse matrix from: https://en.wikipedia.org/wiki/YCbCr#YCbCr
    m = [
        [    kr,                   kg,                kb,          0 ], 
        [ -0.5*kr/(1-kb),    -0.5*kg/(1-kb),         0.5,          0 ],
        [    0.5,            -0.5*kg/(1-kr),    -0.5*kb/(1-kr),    0 ],
        [     0,                   0,                 0,           1 ],
    ]
    im = inv3(m)

    # Extract non-zero components. Verify that we get zero where
    # we expect it.
    c0 = im[0][0]
    assert abs(im[0][1]) < 0.0000001
    c1 = im[0][2]

    assert abs(im[1][0]-c0) < 0.0000001
    c2 = im[1][1]
    c3 = im[1][2]

    assert abs(im[2][0]-c0) < 0.0000001
    c4 = im[2][1]
    assert abs(im[2][2]) < 0.0000001

    print(m[0][0], im[0][0])

    # Our coefficients c0-c4 can be applied as follows to do the conversion:
    #
    #    R = c0 * (Y-y0)*yrange + c1*V*crange
    #    G = c0 * (Y-y0)*yrange + c2*U*crange + c3*V*crange
    #    B = c0 * (Y-y0)*yrange + c4*U*crange
    # 
    # So let's define some aliases which are pre-multiplied by the range:
    # 
    #    C0 = c0*yrange
    #    C1 = c1*crange
    #    C2 = c2*crange
    #    C3 = c3*crange
    #    C4 = c4*crange
    #    
    # Which simplify our formula:
    # 
    #    R = C0 * (Y-y0) + C1*V
    #    G = C0 * (Y-y0) + C2*U + C3*V
    #    B = C0 * (Y-y0) + C4*U
    #
    
    C0 = c0*yrange
    C1 = c1*crange
    C2 = c2*crange
    C3 = c3*crange
    C4 = c4*crange

    # The RDP cannot do exactly this formula. What the RDP does is
    # slightly different, and it does it in two steps. The first step is
    # the texture filter, which calculates:
    # 
    #    TF_R = Y + K0*V
    #    TF_G = Y + K1*U + K2*V
    #    TF_B = Y + K3*U
    # 
    # The second step is the color combiner, which will use the following
    # formula:
    # 
    #    R = (TF_R - K4) * K5 + TF_R = (TF_R - (K4*K5)/(1+K5)) * (1+K5)
    #    G = (TF_G - K4) * K5 + TF_G = (TF_G - (K4*K5)/(1+K5)) * (1+K5)
    #    B = (TF_B - K4) * K5 + TF_B = (TF_B - (K4*K5)/(1+K5)) * (1+K5)
    #    
    # By concatenating the two steps, we find:
    # 
    #    R = (Y + K0*V        - (K4*K5)/(1+K5))) * (1+K5)
    #    G = (Y + K1*U + K2*V - (K4*K5)/(1+K5))) * (1+K5)
    #    B = (Y + K3*U        - (K4*K5)/(1+K5))) * (1+K5)
    # 
    # So let's now compare this with the standard formula above. We need to find
    # a way to express K0..K5 in terms of C0..C4 (plus y0). Let's take
    # the standard formula and factor C0:
    # 
    #    R = (Y - y0 + C1*V/C0)           * C0
    #    G = (Y - y0 + C2*U/C0 + C3*V/C0) * C0
    #    B = (Y - y0 + C4*U/C0)           * C0
    #    
    # We can now derive all coefficients:
    #   
    #    1+K5 = C0              =>    K5 = C0 - 1
    #    (K4*K5)/(1+K5) = y0    =>    K4 = (y0 * (1+K5)) / K5) = y0/K5 + y0
    #
    #    K0 = C1 / C0
    #    K1 = C2 / C0
    #    K2 = C3 / C0
    #    K3 = C4 / C0
    # 

    k5 = C0 - 1
    k4 = y0 / k5 + y0 if k5 else 0
    k0 = C1 / C0
    k1 = C2 / C0
    k2 = C3 / C0
    k3 = C4 / C0

    return [
        int(round(k0*128)),int(round(k1*128)),int(round(k2*128)),int(round(k3*128)),
        int(round(k4*255)),int(round(k5*255)),
    ]

def main():
    print("N64_YCBCR 1.0 - Calculate RDP coefficients for YCbCr conversion")
    print("Coded by Rasky for Libdragon")

    if len(sys.argv) != 6:
        print("\nUsage:")
        print("    n64_ycbcr.py <Kr> <Kb> <Y0> <YRange> <CRange>")
        print("\nWhere:")
        print("  Kr, Kb:  Constants factors that define the color space")
        print("  Y0:      First value of luminance")
        print("  YRange:  Range of luminance")
        print("  CRange:  Range of chrominance")
        print("\nCommon standards:")
        print("  {:>8}  | {:8} {:8} {:4} {:6} {:6}".format("Name", "Kr", "Kb", "Y0", "YRange", "CRange"))
        print("--------------------------------------------------");
        print("  {:>8}  | {:8} {:8} {:4} {:6} {:6}".format("BT.601", "0.299", "0.114", "16", "219", "224"))
        print("  {:>8}  | {:8} {:8} {:4} {:6} {:6}".format("BT.709", "0.2126", "0.0722", "16", "219", "224"))
        sys.exit(1)

    kr = float(sys.argv[1])
    kb = float(sys.argv[2])
    y0 = int(sys.argv[3])
    yrange = int(sys.argv[4])
    crange = int(sys.argv[5])

    assert y0+yrange <= 256, "Invalid y0/range parameters"
    assert crange <= 256,    "Invalid crange parameter"

    # Convert input ranges / offsets into 0..1 scale
    y0 /= 255
    yrange = 256 / yrange
    crange = 256 / crange

    ks = calc(kr, kb, y0, yrange, crange)

    print("RDP coefficients for SetConvert primitive:")
    for i in range(len(ks)):
        print("K{}: {}".format(i, ks[i]))


if __name__ == "__main__":
    main()
