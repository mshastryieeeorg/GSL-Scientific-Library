/* Author:  G. Jungman
 * RCS:     $Id$
 */
#include <stdio.h>
#include <math.h>
#include <gsl_errno.h>
#include <gsl_math.h>
#include "gsl_sf_log.h"
#include "gsl_sf_trig.h"
#include "gsl_sf_gamma.h"

#include "trig_impl.h"

#include "gamma_impl.h"

#define LogRootTwoPi_  0.9189385332046727418
#define LogPi_         1.1447298858494001741
#define Max(a,b) ((a) > (b) ? (a) : (b))


/*-*-*-*-*-*-*-*-*-*-*-* (semi)Private Implementations *-*-*-*-*-*-*-*-*-*-*-*/

/* coefficients for gamma=7, kmax=8  Lanczos method */
static double lanczos_7_c[9] = {
  0.99999999999980993227684700473478,
  676.520368121885098567009190444019,
 -1259.13921672240287047156078755283,
  771.3234287776530788486528258894,
 -176.61502916214059906584551354,
  12.507343278686904814458936853,
 -0.13857109526572011689554707,
  9.984369578019570859563e-6,
  1.50563273514931155834e-7
};

/* Lanczos with gamma=7, truncated at 1/(z+8) 
 * [J. SIAM Numer. Anal, Ser. B, 1 (1964) 86]
 */
int gsl_sf_lngamma_impl(double x, double * result)
{
  if(x <= 0.0) {
    return GSL_EDOM;
  }
  else {
    int k;
    double Ag;

    x -= 1.; /* Lanczos writes z! instead of Gamma(z) */
  
    Ag = lanczos_7_c[0];
    for(k=1; k<=8; k++) { Ag += lanczos_7_c[k]/(x+k); }

    *result =  (x+0.5)*log(x+7.5) - (x+7.5) + LogRootTwoPi_ + log(Ag);
    return GSL_SUCCESS;
  }
}


/* complex version of Lanczos method; this is not safe for export
 * since it becomes bad in the left half-plane
 */
static void lngamma_lanczos_complex(double zr, double zi, double * yr, double * yi)
{
  int k;
  double log1_r,    log1_i;
  double logAg_r,   logAg_i;
  double Ag_r, Ag_i;

  zr -= 1.; /* Lanczos writes z! instead of Gamma(z) */

  Ag_r = lanczos_7_c[0];
  Ag_i = 0.;
  for(k=1; k<=8; k++) {
    double R = zr + k;
    double I = zi;
    double a = lanczos_7_c[k] / (R*R + I*I);
    Ag_r +=  a * R;
    Ag_i -=  a * I;
  }

  gsl_sf_complex_log_impl(zr + 7.5, zi, &log1_r,  &log1_i);
  gsl_sf_complex_log_impl(Ag_r, Ag_i,   &logAg_r, &logAg_i);

  /* (z+0.5)*log(z+7.5) - (z+7.5) + LogRootTwoPi_ + log(Ag(z)) */
  *yr = (zr+0.5)*log1_r - zi*log1_i - (zr+7.5) + LogRootTwoPi_ + logAg_r;
  *yi = zi*log1_r + (zr+0.5)*log1_i - zi + logAg_i;
  gsl_sf_angle_restrict_symm_impl(yi, 1.e-12);
}


int gsl_sf_lngamma_complex_impl(double zr, double zi, double * lnr, double * arg)
{
  if(zr <= 0.5) {
    /* transform to right half plane using reflection;
     * in fact we can do a little better by stopping at 1/2
     */
    int status;
    double x = 1.-zr;
    double y = -zi;
    double a, b;
    double lnsin_r, lnsin_i;
    
    lngamma_lanczos_complex(x, y, &a, &b);
    status = gsl_sf_complex_logsin_impl(M_PI*zr, M_PI*zi, &lnsin_r, &lnsin_i);
    
    if(status == GSL_SUCCESS) {
      *lnr = LogPi_ - lnsin_r - a;
      *arg =        - lnsin_i - b;
      gsl_sf_angle_restrict_symm_impl(arg, 10.*GSL_MACH_EPS);
      return GSL_SUCCESS;
    }
    else {
      return GSL_EDOM;
    }
  }
  else {
    /* otherwise plain vanilla Lanczos */
    lngamma_lanczos_complex(zr, zi, lnr, arg);
    return GSL_SUCCESS;
  }
}


#define FACT_TABLE_MAX  170
#define FACT_TABLE_SIZE (FACT_TABLE_MAX+1)
static struct {int n; double f; long i; } fact_table[FACT_TABLE_SIZE] = {
    { 0,  1.0,     1L     },
    { 1,  1.0,     1L     },
    { 2,  2.0,     2L     },
    { 3,  6.0,     6L     },
    { 4,  24.0,    24L    },
    { 5,  120.0,   120L   },
    { 6,  720.0,   720L   },
    { 7,  5040.0,  5040L  },
    { 8,  40320.0, 40320L },

    { 9,  362880.0,     362880L    },
    { 10, 3628800.0,    3628800L   },
    { 11, 39916800.0,   39916800L  },
    { 12, 479001600.0,  479001600L },

    { 13, 6227020800.0,                               0 },
    { 14, 87178291200.0,                              0 },
    { 15, 1307674368000.0,                            0 },
    { 16, 20922789888000.0,                           0 },
    { 17, 355687428096000.0,                          0 },
    { 18, 6402373705728000.0,                         0 },
    { 19, 121645100408832000.0,                       0 },
    { 20, 2432902008176640000.0,                      0 },
    { 21, 51090942171709440000.0,                     0 },
    { 22, 1124000727777607680000.0,                   0 },
    { 23, 25852016738884976640000.0,                  0 },
    { 24, 620448401733239439360000.0,                 0 },
    { 25, 15511210043330985984000000.0,               0 },
    { 26, 403291461126605635584000000.0,              0 },
    { 27, 10888869450418352160768000000.0,            0 },
    { 28, 304888344611713860501504000000.0,           0 },
    { 29, 8841761993739701954543616000000.0,          0 },
    { 30, 265252859812191058636308480000000.0,        0 },
    { 31, 8222838654177922817725562880000000.0,       0 },
    { 32, 263130836933693530167218012160000000.0,     0 },
    { 33, 8683317618811886495518194401280000000.0,    0 },
    { 34, 2.95232799039604140847618609644e38,  0 },
    { 35, 1.03331479663861449296666513375e40,  0 },
    { 36, 3.71993326789901217467999448151e41,  0 },
    { 37, 1.37637530912263450463159795816e43,  0 },
    { 38, 5.23022617466601111760007224100e44,  0 },
    { 39, 2.03978820811974433586402817399e46,  0 },
    { 40, 8.15915283247897734345611269600e47,  0 },
    { 41, 3.34525266131638071081700620534e49,  0 },
    { 42, 1.40500611775287989854314260624e51,  0 },
    { 43, 6.04152630633738356373551320685e52,  0 },
    { 44, 2.65827157478844876804362581101e54,  0 },
    { 45, 1.19622220865480194561963161496e56,  0 },
    { 46, 5.50262215981208894985030542880e57,  0 },
    { 47, 2.58623241511168180642964355154e59,  0 },
    { 48, 1.24139155925360726708622890474e61,  0 },
    { 49, 6.08281864034267560872252163321e62,  0 },
    { 50, 3.04140932017133780436126081661e64,  0 },
    { 51, 1.55111875328738228022424301647e66,  0 },
    { 52, 8.06581751709438785716606368564e67,  0 },
    { 53, 4.27488328406002556429801375339e69,  0 },
    { 54, 2.30843697339241380472092742683e71,  0 },
    { 55, 1.26964033536582759259651008476e73,  0 },
    { 56, 7.10998587804863451854045647464e74,  0 },
    { 57, 4.05269195048772167556806019054e76,  0 },
    { 58, 2.35056133128287857182947491052e78,  0 },
    { 59, 1.38683118545689835737939019720e80,  0 },
    { 60, 8.32098711274139014427634118320e81,  0 },
    { 61, 5.07580213877224798800856812177e83,  0 },
    { 62, 3.14699732603879375256531223550e85,  0 },
    { 63, 1.982608315404440064116146708360e87,  0 },
    { 64, 1.268869321858841641034333893350e89,  0 },
    { 65, 8.247650592082470666723170306800e90,  0 },
    { 66, 5.443449390774430640037292402480e92,  0 },
    { 67, 3.647111091818868528824985909660e94,  0 },
    { 68, 2.480035542436830599600990418570e96,  0 },
    { 69, 1.711224524281413113724683388810e98,  0 },
    { 70, 1.197857166996989179607278372170e100,  0 },
    { 71, 8.504785885678623175211676442400e101,  0 },
    { 72, 6.123445837688608686152407038530e103,  0 },
    { 73, 4.470115461512684340891257138130e105,  0 },
    { 74, 3.307885441519386412259530282210e107,  0 },
    { 75, 2.480914081139539809194647711660e109,  0 },
    { 76, 1.885494701666050254987932260860e111,  0 },
    { 77, 1.451830920282858696340707840860e113,  0 },
    { 78, 1.132428117820629783145752115870e115,  0 },
    { 79, 8.946182130782975286851441715400e116,  0 },
    { 80, 7.156945704626380229481153372320e118,  0 },
    { 81, 5.797126020747367985879734231580e120,  0 },
    { 82, 4.753643337012841748421382069890e122,  0 },
    { 83, 3.945523969720658651189747118010e124,  0 },
    { 84, 3.314240134565353266999387579130e126,  0 },
    { 85, 2.817104114380550276949479442260e128,  0 },
    { 86, 2.422709538367273238176552320340e130,  0 },
    { 87, 2.107757298379527717213600518700e132,  0 },
    { 88, 1.854826422573984391147968456460e134,  0 },
    { 89, 1.650795516090846108121691926250e136,  0 },
    { 90, 1.485715964481761497309522733620e138,  0 },
    { 91, 1.352001527678402962551665687590e140,  0 },
    { 92, 1.243841405464130725547532432590e142,  0 },
    { 93, 1.156772507081641574759205162310e144,  0 },
    { 94, 1.087366156656743080273652852570e146,  0 },
    { 95, 1.032997848823905926259970209940e148,  0 },
    { 96, 9.916779348709496892095714015400e149,  0 },
    { 97, 9.619275968248211985332842594960e151,  0 },
    { 98, 9.426890448883247745626185743100e153,  0 },
    { 99, 9.332621544394415268169923885600e155,  0 },
    { 100, 9.33262154439441526816992388563e157,  0 },
    { 101, 9.42594775983835942085162312450e159,  0 },
    { 102, 9.61446671503512660926865558700e161,  0 },
    { 103, 9.90290071648618040754671525458e163,  0 },
    { 104, 1.02990167451456276238485838648e166,  0 },
    { 105, 1.08139675824029090050410130580e168,  0 },
    { 106, 1.146280563734708354534347384148e170,  0 },
    { 107, 1.226520203196137939351751701040e172,  0 },
    { 108, 1.324641819451828974499891837120e174,  0 },
    { 109, 1.443859583202493582204882102460e176,  0 },
    { 110, 1.588245541522742940425370312710e178,  0 },
    { 111, 1.762952551090244663872161047110e180,  0 },
    { 112, 1.974506857221074023536820372760e182,  0 },
    { 113, 2.231192748659813646596607021220e184,  0 },
    { 114, 2.543559733472187557120132004190e186,  0 },
    { 115, 2.925093693493015690688151804820e188,  0 },
    { 116, 3.393108684451898201198256093590e190,  0 },
    { 117, 3.96993716080872089540195962950e192,  0 },
    { 118, 4.68452584975429065657431236281e194,  0 },
    { 119, 5.57458576120760588132343171174e196,  0 },
    { 120, 6.68950291344912705758811805409e198,  0 },
    { 121, 8.09429852527344373968162284545e200,  0 },
    { 122, 9.87504420083360136241157987140e202,  0 },
    { 123, 1.21463043670253296757662432419e205,  0 },
    { 124, 1.50614174151114087979501416199e207,  0 },
    { 125, 1.88267717688892609974376770249e209,  0 },
    { 126, 2.37217324288004688567714730514e211,  0 },
    { 127, 3.01266001845765954480997707753e213,  0 },
    { 128, 3.85620482362580421735677065923e215,  0 },
    { 129, 4.97450422247728744039023415041e217,  0 },
    { 130, 6.46685548922047367250730439554e219,  0 },
    { 131, 8.47158069087882051098456875820e221,  0 },
    { 132, 1.11824865119600430744996307608e224,  0 },
    { 133, 1.48727070609068572890845089118e226,  0 },
    { 134, 1.99294274616151887673732419418e228,  0 },
    { 135, 2.69047270731805048359538766215e230,  0 },
    { 136, 3.65904288195254865768972722052e232,  0 },
    { 137, 5.01288874827499166103492629211e234,  0 },
    { 138, 6.91778647261948849222819828311e236,  0 },
    { 139, 9.61572319694108900419719561353e238,  0 },
    { 140, 1.34620124757175246058760738589e241,  0 },
    { 141, 1.89814375907617096942852641411e243,  0 },
    { 142, 2.69536413788816277658850750804e245,  0 },
    { 143, 3.85437071718007277052156573649e247,  0 },
    { 144, 5.55029383273930478955105466055e249,  0 },
    { 145, 8.04792605747199194484902925780e251,  0 },
    { 146, 1.17499720439091082394795827164e254,  0 },
    { 147, 1.72724589045463891120349865931e256,  0 },
    { 148, 2.55632391787286558858117801578e258,  0 },
    { 149, 3.80892263763056972698595524351e260,  0 },
    { 150, 5.71338395644585459047893286526e262,  0 },
    { 151, 8.62720977423324043162318862650e264,  0 },
    { 152, 1.31133588568345254560672467123e267,  0 },
    { 153, 2.00634390509568239477828874699e269,  0 },
    { 154, 3.08976961384735088795856467036e271,  0 },
    { 155, 4.78914290146339387633577523906e273,  0 },
    { 156, 7.47106292628289444708380937294e275,  0 },
    { 157, 1.17295687942641442819215807155e278,  0 },
    { 158, 1.85327186949373479654360975305e280,  0 },
    { 159, 2.94670227249503832650433950735e282,  0 },
    { 160, 4.71472363599206132240694321176e284,  0 },
    { 161, 7.59070505394721872907517857094e286,  0 },
    { 162, 1.22969421873944943411017892849e289,  0 },
    { 163, 2.00440157654530257759959165344e291,  0 },
    { 164, 3.28721858553429622726333031164e293,  0 },
    { 165, 5.42391066613158877498449501421e295,  0 },
    { 166, 9.00369170577843736647426172359e297,  0 },
    { 167, 1.50361651486499904020120170784e300,  0 },
    { 168, 2.52607574497319838753801886917e302,  0 },
    { 169, 4.26906800900470527493925188890e304,  0 },
    { 170, 7.25741561530799896739672821113e306,  0 },

    /*
    { 171, 1.24101807021766782342484052410e309,  0 },
    { 172, 2.13455108077438865629072570146e311,  0 },
    { 173, 3.69277336973969237538295546352e313,  0 },
    { 174, 6.42542566334706473316634250653e315,  0 },
    { 175, 1.12444949108573632830410993864e318,  0 },
    { 176, 1.97903110431089593781523349201e320,  0 },
    { 177, 3.50288505463028580993296328086e322,  0 },
    { 178, 6.23513539724190874168067463993e324,  0 },
    { 179, 1.11608923610630166476084076055e327,  0 },
    { 180, 2.00896062499134299656951336898e329,  0 },
    { 181, 3.63621873123433082379081919786e331,  0 },
    { 182, 6.61791809084648209929929094011e333,  0 },
    { 183, 1.21107901062490622417177024204e336,  0 },
    { 184, 2.22838537954982745247605724535e338,  0 },
    { 185, 4.12251295216718078708070590390e340,  0 },
    { 186, 7.66787409103095626397011298130e342,  0 },
    { 187, 1.43389245502278882136241112750e345,  0 },
    { 188, 2.69571781544284298416133291969e347,  0 },
    { 189, 5.09490667118697324006491921822e349,  0 },
    { 190, 9.68032267525524915612334651460e351,  0 },
    { 191, 1.84894163097375258881955918429e354,  0 },
    { 192, 3.54996793146960497053355363384e356,  0 },
    { 193, 6.85143810773633759312975851330e358,  0 },
    { 194, 1.32917899290084949306717315158e361,  0 },
    { 195, 2.59189903615665651148098764559e363,  0 },
    { 196, 5.08012211086704676250273578535e365,  0 },
    { 197, 1.00078405584080821221303894971e368,  0 },
    { 198, 1.98155243056480026018181712043e370,  0 },
    { 199, 3.94328933682395251776181606966e372,  0 },
    { 200, 7.88657867364790503552363213932e374,  0 }
    */
};


int gsl_sf_fact_impl(const int n, double * result)
{
 if(n < 0) {
    return GSL_EDOM;
  }
  else if(n <= FACT_TABLE_MAX){
    *result = fact_table[n].f;
    return GSL_SUCCESS;
  }
  else {
    return GSL_EOVRFLW;
  }
}

int gsl_sf_lnfact_impl(const int n, double * result)
{
  if(n < 0) {
    return GSL_EDOM;
  }
  else if(n <= FACT_TABLE_MAX){
    *result = log(fact_table[n].f);
    return GSL_SUCCESS;
  }
  else {
    gsl_sf_lngamma_impl(n+1., result);
    return GSL_SUCCESS;
  }
}

int gsl_sf_lnchoose_impl(unsigned int n, unsigned int m, double * result)
{
  double nf, mf, nmmf;

  if(m > n) return GSL_EDOM;

  if(m*2 > n) m = n-m;
  gsl_sf_lnfact_impl(n+1, &nf);
  gsl_sf_lnfact_impl(m+1, &mf);
  gsl_sf_lnfact_impl(n-m+1, &nmmf);
  *result = nf - mf - nmmf;
  return GSL_SUCCESS;
}

int gsl_sf_choose_impl(unsigned int n, unsigned int m, double * result)
{
  double nf, mf, nmmf;
  double ln_result;

  if(m > n) return GSL_EDOM;

  if(m*2 > n) m = n-m;
  gsl_sf_lnfact_impl(n+1, &nf);
  gsl_sf_lnfact_impl(m+1, &mf);
  gsl_sf_lnfact_impl(n-m+1, &nmmf);
  ln_result = nf - mf - nmmf;
  
  if(ln_result < GSL_LOG_DBL_MAX) {
    *result = exp(ln_result);
    return GSL_SUCCESS;
  }
  else {
    return GSL_EOVRFLW;
  }
}


/*-*-*-*-*-*-*-*-*-*-*-* Functions w/ Error Handling *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_fact_e(const int n, double * result)
{
  int status = gsl_sf_fact_impl(n, result);
  if(status != GSL_SUCCESS) {
    GSL_ERROR("gsl_sf_fact_e", status);
  }
  return status;
}

int gsl_sf_lnfact_e(const int n, double * result)
{
  int status = gsl_sf_lnfact_impl(n, result);
  if(status != GSL_SUCCESS) {
    GSL_ERROR("gsl_sf_lnfact_e", status);
  }
  return status;
}

int gsl_sf_lngamma_e(const double x, double * result)
{
  int status = gsl_sf_lngamma_impl(x, result);
  if(status != GSL_SUCCESS) {
    GSL_ERROR("gsl_sf_lngamma_e", status);
  }
  return status;
}

int gsl_sf_lngamma_complex_e(double zr, double zi, double * lnr, double * arg)
{
  int status = gsl_sf_lngamma_complex_impl(zr, zi, lnr, arg);
  if(status != GSL_SUCCESS) {;
    GSL_ERROR("gsl_sf_lngamma_complex_e", status);
  }
  return status;
}

int gsl_sf_choose_e(unsigned int n, unsigned int m, double * r)
{
  int status = gsl_sf_choose_impl(n, m, r);
  if(status != GSL_SUCCESS) {;
    GSL_ERROR("gsl_sf_choose_e", status);
  }
  return status;
}

int gsl_sf_lnchoose_e(unsigned int n, unsigned int m, double * r)
{
  int status = gsl_sf_lnchoose_impl(n, m, r);
  if(status != GSL_SUCCESS) {;
    GSL_ERROR("gsl_sf_lnchoose_e", status);
  }
  return status;
}


/*-*-*-*-*-*-*-*-*-*-*-* Functions w/ Natural Prototypes *-*-*-*-*-*-*-*-*-*-*-*/

double gsl_sf_lngamma(const double x)
{
  double y;
  int status = gsl_sf_lngamma_impl(x, &y);
  if(status != GSL_SUCCESS) {
    GSL_WARNING("gsl_sf_lngamma", status);
  }
  return y;
}

double gsl_sf_lnfact(const int n)
{
  double y;
  int status = gsl_sf_lnfact_impl(n, &y);
  if(status != GSL_SUCCESS) {
    GSL_WARNING("gsl_sf_lnfact", status);
  }
  return y;
}
