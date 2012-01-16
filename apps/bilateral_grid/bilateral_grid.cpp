#include "FImage.h"

using namespace FImage;

Expr lerp(Expr a, Expr b, Expr alpha) {
    return (1.0f - alpha)*a + alpha*b;
}

int main(int argc, char **argv) {
    UniformImage input(Float(32), 2);
    Uniform<float> r_sigma;
    Uniform<int> s_sigma;
    Var x, y, z, c;

    // Add a boundary condition 
    Func clamped;
    clamped(x, y) = input(clamp(x, 0, input.width()),
                          clamp(y, 0, input.height()));                                

    // Construct the bilateral grid 
    RVar i(0, s_sigma), j(0, s_sigma);
    Expr val = clamped(x * s_sigma + i - s_sigma/2, y * s_sigma + j - s_sigma/2);
    val = clamp(val, 0.0f, 1.0f);
    Expr zi = cast<int>(val * (1.0f/r_sigma) + 0.5f);
    Func grid;
    grid(x, y, zi, c) += select(c == 0, val, 1.0f);

    // Blur the grid using a five-tap filter
    Func blurx, blury, blurz;
    blurx(x, y, z) = grid(x-2, y, z) + grid(x-1, y, z)*4 + grid(x, y, z)*6 + grid(x+1, y, z)*4 + grid(x+1, y, z);
    blury(x, y, z) = blurx(x, y-2, z) + blurx(x, y-1, z)*4 + blurx(x, y, z)*6 + blurx(x, y+1, z)*4 + blurx(x, y+2, z);
    blurz(x, y, z) = blury(x, y, z-2) + blury(x, y, z-1)*4 + blury(x, y, z)*6 + blury(x, y, z+1)*4 + blury(x, y, z+2);

    // Take trilinear samples to compute the output
    val = clamp(clamped(x, y), 0.0f, 1.0f);
    Expr zv = val * (1.0f/r_sigma);
    zi = cast<int>(zv);
    Expr zf = zv - zi;
    Expr xf = cast<float>(x % s_sigma) / s_sigma;
    Expr yf = cast<float>(y % s_sigma) / s_sigma;
    Expr xi = x/s_sigma;
    Expr yi = y/s_sigma;
    Func interpolated;
    interpolated(x, y) = 
        lerp(lerp(lerp(blurz(xi, yi, zi), blurz(xi+1, yi, zi), xf),
                  lerp(blurz(xi, yi+1, zi), blurz(xi+1, yi+1, zi), xf), yf),
             lerp(lerp(blurz(xi, yi, zi+1), blurz(xi+1, yi, zi+1), xf),
                  lerp(blurz(xi, yi+1, zi+1), blurz(xi+1, yi+1, zi+1), xf), yf), zf);

    // Normalize
    Func smoothed;
    smoothed(x, y) = interpolated(x, y, 0)/interpolated(x, y, 1);

    grid.root().parallel(z);
    grid.update().transpose(y, c).transpose(x, c).transpose(i, c).transpose(j, c).parallel(y);
    blurx.root().parallel(z).vectorize(x, 4);
    blury.root().parallel(z).vectorize(x, 4);
    blurz.root().parallel(z).vectorize(x, 4);
    smoothed.root().parallel(y).vectorize(x, 4); 

    smoothed.compileToFile("bilateral_grid");

    // Compared to Sylvain Paris' implementation from his webpage (on
    // which this is based), for filter params s_sigma 0.1, on a 4 megapixel
    // input, on a four core x86 (2 socket core2 mac pro)
    // Filter s_sigma: 2      4       8       16      32
    // Paris (ms):     5350   1345    472     245     184
    // Us (ms):        383    142     77      62      65
    // Speedup:        14     9.5     6.1     3.9     2.8

    // Our schedule and inlining are roughly the same as his, so the
    // gain is all down to vectorizing and parallelizing. In general
    // for larger blurs our win shrinks to roughly the number of
    // cores, as the stages we don't vectorize as well dominate (we
    // don't vectorize them well because they do gathers and scatters,
    // which don't work well on x86).  For smaller blurs, our win
    // grows, because the stages that we vectorize take up all the
    // time.
    

    return 0;
}



