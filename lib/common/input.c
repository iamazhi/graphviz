/* $Id$ $Revision$ */
/* vim:set shiftwidth=4 ts=8: */

/**********************************************************
*      This software is part of the graphviz package      *
*                http://www.graphviz.org/                 *
*                                                         *
*            Copyright (c) 1994-2004 AT&T Corp.           *
*                and is licensed under the                *
*            Common Public License, Version 1.0           *
*                      by AT&T Corp.                      *
*                                                         *
*        Information and Software Systems Research        *
*              AT&T Research, Florham Park NJ             *
**********************************************************/

#include <ctype.h>
#include "render.h"
#include "htmltable.h"

static char *usageFmt =
    "Usage: %s [-Vv?] [-(GNE)name=val] [-(KTlso)<val>] <dot files>\n";

static char *genericItems = "\n\
 -V          - Print version and exit\n\
 -v          - Enable verbose mode \n\
 -Gname=val  - Set graph attribute 'name' to 'val'\n\
 -Nname=val  - Set node attribute 'name' to 'val'\n\
 -Ename=val  - Set edge attribute 'name' to 'val'\n\
 -Tv         - Set output format to 'v'\n\
 -Kv         - Set layout engine to 'v' (overrides default based on command name)\n\
 -lv         - Use external library 'v'\n\
 -ofile      - Write output to 'file'\n\
 -O          - Automatically generate an output filename based on the input filename with a .'format' appended. (Causes all -ofile options to be ignored.) \n\
 -P          - Internally generate a graph of the current plugins. \n\
 -q[l]       - Set level of message suppression (=1)\n\
 -s[v]       - Scale input by 'v' (=72)\n\
 -y          - Invert y coordinate in output\n";

static char *neatoFlags =
    "(additional options for neato)    [-x] [-n<v>]\n";
static char *neatoItems = "\n\
 -n[v]       - No layout mode 'v' (=1)\n\
 -x          - Reduce graph\n";

static char *fdpFlags =
    "(additional options for fdp)      [-L(gO)] [-L(nUCT)<val>]\n";
static char *fdpItems = "\n\
 -Lg         - Don't use grid\n\
 -LO         - Use old attractive force\n\
 -Ln<i>      - Set number of iterations to i\n\
 -LU<i>      - Set unscaled factor to i\n\
 -LC<v>      - Set overlap expansion factor to v\n\
 -LT[*]<v>   - Set temperature (temperature factor) to v\n";

static char *memtestFlags = "(additional options for memtest)  [-m]\n";
static char *memtestItems = "\n\
 -m          - Memory test (Observe no growth with top. Kill when done.)\n";

static char *configFlags = "(additional options for config)  [-cv]\n";
static char *configItems = "\n\
 -c          - Configure plugins (Writes $prefix/lib/graphviz/config \n\
               with available plugin information.  Needs write privilege.)\n\
 -v          - Enable verbose mode \n";

void dotneato_usage(int exval)
{
    FILE *outs;

    if (exval > 0)
	outs = stderr;
    else
	outs = stdout;

    fprintf(outs, usageFmt, CmdName);
    fputs(neatoFlags, outs);
    fputs(fdpFlags, outs);
    fputs(memtestFlags, outs);
    fputs(configFlags, outs);
    fputs(genericItems, outs);
    fputs(neatoItems, outs);
    fputs(fdpItems, outs);
    fputs(memtestItems, outs);
    fputs(configItems, outs);

    if (exval >= 0)
	exit(exval);
}

/* getFlagOpt:
 * Look for flag parameter. idx is index of current argument.
 * We assume argv[*idx] has the form "-x..." If there are characters 
 * after the x, return
 * these, else if there are more arguments, return the next one,
 * else return NULL.
 */
static char *getFlagOpt(int argc, char **argv, int *idx)
{
    int i = *idx;
    char *arg = argv[i];

    if (arg[2])
	return arg + 2;
    if (i < argc - 1) {
	i++;
	arg = argv[i];
	if (*arg && (*arg != '-')) {
	    *idx = i;
	    return arg;
	}
    }
    return 0;
}

/* dotneato_basename:
 * Partial implementation of real basename.
 * Skip over any trailing slashes or backslashes; then
 * find next (back)slash moving left; return string to the right.
 * If no next slash is found, return the whole string.
 */
static char* dotneato_basename (char* path)
{
    char* ret;
    char* s = path;
    if (*s == '\0') return path; /* empty string */
#ifdef WIN32
    /* On Windows, executables, by convention, end in ".exe". Thus,
     * this may be part of the path name and must be removed for
     * matching to work.
     */
    {
	char* dotp = strrchr (s, '.');
	if (dotp && !strcasecmp(dotp+1,"exe")) *dotp = '\0';
    }
#endif
    while (*s) s++; s--;
    /* skip over trailing slashes, nulling out as we go */
    while ((s > path) && ((*s == '/') || (*s == '\\')))
	*s-- = '\0';
    if (s == path) ret = path;
    else {
	while ((s > path) && ((*s != '/') && (*s != '\\'))) s--;
	if ((*s == '/') || (*s == '\\')) ret = s+1;
	else ret = path;
    }
#ifdef WIN32
    /* On Windows, names are case-insensitive, so make name lower-case
     */
    {
	char c;
	for (s = ret; (c = *s); s++)
	    *s = tolower(c);
    }
#endif
    return ret;
}

static void use_library(GVC_t *gvc, const char *name)
{
    static int cnt = 0;
    if (name) {
	Lib = ALLOC(cnt + 2, Lib, const char *);
	Lib[cnt++] = name;
	Lib[cnt] = NULL;
    }
    gvc->common.lib = Lib;
}

#ifdef WITH_CGRAPH
static void global_def(char *dcl, int kind,
         attrsym_t * ((*dclfun) (Agraph_t *, int kind, char *, char *)) )
{
    char *p;
    char *rhs = "true";

    attrsym_t *sym;
    if ((p = strchr(dcl, '='))) {
        *p++ = '\0';
        rhs = p;
    }
    sym = dclfun(NULL, kind, dcl, rhs);
    sym->fixed = 1;
}
#else
static void global_def(char *dcl,
	attrsym_t * ((*dclfun) (Agraph_t *, char *, char *)))
{
    char *p;
    char *rhs = "true";

    attrsym_t *sym;
    if ((p = strchr(dcl, '='))) {
	*p++ = '\0';
	rhs = p;
    }
    sym = dclfun(NULL, dcl, rhs);
    sym->fixed = 1;
}
#endif

static void gvg_init(GVC_t *gvc, graph_t *g, char *fn, int gidx)
{
    GVG_t *gvg;

    gvg = zmalloc(sizeof(GVG_t));
    if (!gvc->gvgs) 
	gvc->gvgs = gvg;
    else
	gvc->gvg->next = gvg;
    gvc->gvg = gvg;
    gvg->gvc = gvc;
    gvg->g = g;
    gvg->input_filename = fn;
    gvg->graph_index = gidx;
}

static graph_t *P_graph;

graph_t *gvPluginsGraph(GVC_t *gvc)
{
    gvg_init(gvc, P_graph, "<internal>", 0);
    return P_graph;
}

void dotneato_args_initialize(GVC_t * gvc, int argc, char **argv)
{
    char c, *rest;
    const char *val;
    int i, v, nfiles;

    /* establish if we are running in a CGI environment */
    HTTPServerEnVar = getenv("SERVER_NAME");

    /* establish Gvfilepath, if any */
    Gvfilepath = getenv("GV_FILE_PATH");

    gvc->common.cmdname = dotneato_basename(argv[0]);
    if (gvc->common.verbose) {
        fprintf(stderr, "%s - %s version %s (%s)\n",
	    gvc->common.cmdname, gvc->common.info[0],
	    gvc->common.info[1], gvc->common.info[2]);
    }

    /* configure for available plugins and codegens */
    gvconfig(gvc, gvc->common.config);
    if (gvc->common.config)
	exit (0);

    i = gvlayout_select(gvc, gvc->common.cmdname);
    if (i == NO_SUPPORT)
	gvlayout_select(gvc, "dot");

    /* feed the globals */
    Verbose = gvc->common.verbose;
    CmdName = gvc->common.cmdname;

#ifndef WITH_CGRAPH
    aginit();
#endif
    nfiles = 0;
    for (i = 1; i < argc; i++)
	if (argv[i] && argv[i][0] != '-')
	    nfiles++;
    gvc->input_filenames = N_NEW(nfiles + 1, char *);
    nfiles = 0;
    for (i = 1; i < argc; i++) {
	if (argv[i] && argv[i][0] == '-') {
	    rest = &(argv[i][2]);
	    switch (c = argv[i][1]) {
	    case 'G':
		if (*rest)
#ifdef WITH_CGRAPH
		    global_def(rest, AGRAPH, agattr);
#else
		    global_def(rest, agraphattr);
#endif
		else {
		    fprintf(stderr, "Missing argument for -G flag\n");
		    dotneato_usage(1);
		}
		break;
	    case 'N':
		if (*rest)
#ifdef WITH_CGRAPH
		    global_def(rest, AGNODE,agattr);
#else
		    global_def(rest, agnodeattr);
#endif
		else {
		    fprintf(stderr, "Missing argument for -N flag\n");
		    dotneato_usage(1);
		}
		break;
	    case 'E':
		if (*rest)
#ifdef WITH_CGRAPH
		    global_def(rest, AGEDGE,agattr);
#else
		    global_def(rest, agedgeattr);
#endif
		else {
		    fprintf(stderr, "Missing argument for -E flag\n");
		    dotneato_usage(1);
		}
		break;
	    case 'T':
		val = getFlagOpt(argc, argv, &i);
		if (!val) {
		    fprintf(stderr, "Missing argument for -T flag\n");
		    dotneato_usage(1);
		    exit(1);
		}
		v = gvjobs_output_langname(gvc, val);
		if (!v) {
		    fprintf(stderr, "Format: \"%s\" not recognized. Use one of:%s\n",
			val, gvplugin_list(gvc, API_device, val));
		    exit(1);
		}
		break;
	    case 'K':
		val = getFlagOpt(argc, argv, &i);
		if (!val) {
                    fprintf(stderr, "Missing argument for -K flag\n");
                    dotneato_usage(1);
                    exit(1);
                }
                v = gvlayout_select(gvc, val);
                if (v == NO_SUPPORT) {
                    fprintf(stderr, "Layout type: \"%s\" not recognized. Use one of:%s\n",
                        val, gvplugin_list(gvc, API_layout, val));
                    exit(1);
                }
		break;
	    case 'P':
		P_graph = gvplugin_graph(gvc);
		break;
	    case 'V':
		fprintf(stderr, "%s - %s version %s (%s)\n",
			gvc->common.cmdname, gvc->common.info[0], 
			gvc->common.info[1], gvc->common.info[2]);
		exit(0);
		break;
	    case 'l':
		val = getFlagOpt(argc, argv, &i);
		if (!val) {
		    fprintf(stderr, "Missing argument for -l flag\n");
		    dotneato_usage(1);
		}
		use_library(gvc, val);
		break;
	    case 'o':
		val = getFlagOpt(argc, argv, &i);
		if (! gvc->common.auto_outfile_names)
		    gvjobs_output_filename(gvc, val);
		break;
	    case 'q':
		if (*rest) {
		    v = atoi(rest);
		    if (v <= 0) {
			fprintf(stderr,
				"Invalid parameter \"%s\" for -q flag - ignored\n",
				rest);
		    } else if (v == 1)
			agseterr(AGERR);
		    else
			agseterr(AGMAX);
		} else
		    agseterr(AGERR);
		break;
	    case 's':
		if (*rest) {
		    PSinputscale = atof(rest);
		    if (PSinputscale <= 0) {
			fprintf(stderr,
				"Invalid parameter \"%s\" for -s flag\n",
				rest);
			dotneato_usage(1);
		    }
		} else
		    PSinputscale = POINTS_PER_INCH;
		break;
	    case 'x':
		Reduce = TRUE;
		break;
	    case 'y':
		Y_invert = TRUE;
		break;
	    case '?':
		dotneato_usage(0);
		break;
	    default:
		fprintf(stderr, "%s: option -%c unrecognized\n\n", gvc->common.cmdname,
			c);
		dotneato_usage(1);
	    }
	} else if (argv[i])
	    gvc->input_filenames[nfiles++] = argv[i];
    }

    /* if no -Txxx, then set default format */
    if (!gvc->jobs || !gvc->jobs->output_langname) {
	v = gvjobs_output_langname(gvc, "dot");
	assert(v);  /* "dot" should always be available as an output format */
    }

    /* set persistent attributes here (if not already set from command line options) */
#ifndef WITH_CGRAPH
    if (!(agfindattr(agprotograph()->proto->n, "label")))
	agnodeattr(NULL, "label", NODENAME_ESC);
#endif
}

/* getdoubles2ptf:
 * converts a graph attribute to floating graph units (POINTS).
 * Returns true if the attribute ends in '!'.
 */
static boolean getdoubles2ptf(graph_t * g, char *name, pointf * result)
{
    char *p;
    int i;
    double xf, yf;
    char c = '\0';
    boolean rv = FALSE;

    if ((p = agget(g, name))) {
	i = sscanf(p, "%lf,%lf%c", &xf, &yf, &c);
	if ((i > 1) && (xf > 0) && (yf > 0)) {
	    result->x = POINTS(xf);
	    result->y = POINTS(yf);
	    if (c == '!')
		rv = TRUE;
	}
    }
    return rv;
}

void getdouble(graph_t * g, char *name, double *result)
{
    char *p;
    double f;

    if ((p = agget(g, name))) {
	if (sscanf(p, "%lf", &f) >= 1)
	    *result = f;
    }
}

#ifdef EXPERIMENTAL_MYFGETS
/*
 * Potential input filter - e.g. for iconv 
 */

/*
 * myfgets - same api as fgets
 * 
 * gets n chars at a time
 *
 * returns pointer to user buffer,
 * or returns NULL on eof or error.
 */
static char *myfgets(char * ubuf, int n, FILE * fp)
{
    static char *buf;
    static int bufsz, pos, len;
    int cnt;

    if (!n) {                   /* a call with n==0 (from aglexinit) resets */
        ubuf[0] = '\0';
        pos = len = 0;
        return NULL;
    }

    if (!len) {
	if (n > bufsz) {
	    bufsz = n;
	    buf = realloc(buf, bufsz);
	}
	if (!(fgets(buf, bufsz, fp))) {
            ubuf[0] = '\0';
            return NULL;
        }
	len = strlen(buf);
	pos = 0;
    }

    cnt = n - 1;
    if (len < cnt)
	cnt = len;

    memcpy(ubuf, buf + pos, cnt);
    pos += cnt;
    len -= cnt;
    ubuf[cnt] = '\0';

    return ubuf;
}
#endif

graph_t *gvNextInputGraph(GVC_t *gvc)
{
    graph_t *g = NULL;
    static char *fn;
    static FILE *fp;
    static int fidx, gidx;

    while (!g) {
	if (!fp) {
    	    if (!(fn = gvc->input_filenames[0])) {
		if (fidx++ == 0)
		    fp = stdin;
	    }
	    else {
		while ((fn = gvc->input_filenames[fidx++]) && !(fp = fopen(fn, "r")))  {
		    agerr(AGERR, "%s: can't open %s\n", gvc->common.cmdname, fn);
		    graphviz_errors++;
		}
	    }
	}
	if (fp == NULL)
	    break;
	agsetfile(fn ? fn : "<stdin>");
#ifdef EXPERIMENTAL_MYFGETS
	g = agread_usergets(fp, myfgets);
#else
#ifdef WITH_CGRAPH
	g = agread(fp,NIL(Agdisc_t*));
#else
	g = agread(fp);
#endif
#endif
	if (g) {
	    gvg_init(gvc, g, fn, gidx++);
	    break;
	}
	fp = NULL;
	gidx = 0;
    }
    return g;
}

/* findCharset:
 * Check if the charset attribute is defined for the graph and, if
 * so, return the corresponding internal value. If undefined, return
 * CHAR_UTF8
 */
static int findCharset (graph_t * g)
{
    int enc;
    char* p;

#ifdef WITH_CGRAPH
    p = late_nnstring(g,agattr(g,AGRAPH,"charset", NULL),"utf-8");
#else
    p = late_nnstring(g,agfindattr(g,"charset"),"utf-8");
#endif
    if (!strcasecmp(p,"latin-1")
	|| !strcasecmp(p,"latin1")
	|| !strcasecmp(p,"l1")
	|| !strcasecmp(p,"ISO-8859-1")
	|| !strcasecmp(p,"ISO_8859-1")
	|| !strcasecmp(p,"ISO8859-1")
	|| !strcasecmp(p,"ISO-IR-100"))
		enc = CHAR_LATIN1; 
    else if (!strcasecmp(p,"big-5")
	|| !strcasecmp(p,"big5")) 
		enc = CHAR_BIG5; 
    else if (!strcasecmp(p,"utf-8")
	|| !strcasecmp(p,"utf8"))
		enc = CHAR_UTF8; 
    else {
	agerr(AGWARN, "Unsupported charset \"%s\" - assuming utf-8\n", p);
	enc = CHAR_UTF8; 
    }
    return enc;
}

/* setRatio:
 * Checks "ratio" attribute, if any, and sets enum type.
 */
static void setRatio(graph_t * g)
{
    char *p, c;
    double ratio;

    if ((p = agget(g, "ratio")) && ((c = p[0]))) {
	switch (c) {
	case 'a':
	    if (streq(p, "auto"))
		GD_drawing(g)->ratio_kind = R_AUTO;
	    break;
	case 'c':
	    if (streq(p, "compress"))
		GD_drawing(g)->ratio_kind = R_COMPRESS;
	    break;
	case 'e':
	    if (streq(p, "expand"))
		GD_drawing(g)->ratio_kind = R_EXPAND;
	    break;
	case 'f':
	    if (streq(p, "fill"))
		GD_drawing(g)->ratio_kind = R_FILL;
	    break;
	default:
	    ratio = atof(p);
	    if (ratio > 0.0) {
		GD_drawing(g)->ratio_kind = R_VALUE;
		GD_drawing(g)->ratio = ratio;
	    }
	    break;
	}
    }
}

/*
	cgraph requires 

*/
void graph_init(graph_t * g, boolean use_rankdir)
{
    char *p;
    double xf;
    static char *rankname[] = { "local", "global", "none", NULL };
    static int rankcode[] = { LOCAL, GLOBAL, NOCLUST, LOCAL };
    static char *fontnamenames[] = {"gd","ps","svg", NULL};
    static int fontnamecodes[] = {NATIVEFONTS,PSFONTS,SVGFONTS,-1};
    int rankdir;

    GD_drawing(g) = NEW(layout_t);

    /* set this up fairly early in case any string sizes are needed */
    if ((p = agget(g, "fontpath")) || (p = getenv("DOTFONTPATH"))) {
	/* overide GDFONTPATH in local environment if dot
	 * wants its own */
#ifdef HAVE_SETENV
	setenv("GDFONTPATH", p, 1);
#else
	static char *buf = 0;

	buf = grealloc(buf, strlen("GDFONTPATH=") + strlen(p) + 1);
	strcpy(buf, "GDFONTPATH=");
	strcat(buf, p);
	putenv(buf);
#endif
    }

    GD_charset(g) = findCharset (g);

#ifdef WITH_CGRAPH
    GD_drawing(g)->quantum =
	late_double(g, agattr(g, AGRAPH, "quantum", NULL), 0.0, 0.0);
#else
    GD_drawing(g)->quantum =
	late_double(g, agfindattr(g, "quantum"), 0.0, 0.0);
#endif

    /* setting rankdir=LR is only defined in dot,
     * but having it set causes shape code and others to use it. 
     * The result is confused output, so we turn it off unless requested.
     * This effective rankdir is stored in the bottom 2 bits of g->u.rankdir.
     * Sometimes, the code really needs the graph's rankdir, e.g., neato -n
     * with record shapes, so we store the real rankdir in the next 2 bits.
     */
    rankdir = RANKDIR_TB;
    if ((p = agget(g, "rankdir"))) {
	if (streq(p, "LR"))
	    rankdir = RANKDIR_LR;
	else if (streq(p, "BT"))
	    rankdir = RANKDIR_BT;
	else if (streq(p, "RL"))
	    rankdir = RANKDIR_RL;
    }
    if (use_rankdir)
	SET_RANKDIR (g, (rankdir << 2) | rankdir);
    else
	SET_RANKDIR (g, (rankdir << 2));

#ifdef WITH_CGRAPH
    xf = late_double(g, agattr(g, AGRAPH, "nodesep", NULL),
		DEFAULT_NODESEP, MIN_NODESEP);
#else
    xf = late_double(g, agfindattr(g, "nodesep"),
		DEFAULT_NODESEP, MIN_NODESEP);
#endif
    GD_nodesep(g) = POINTS(xf);

#ifdef WITH_CGRAPH
    p = late_string(g, agattr(g, AGRAPH, "ranksep", (char*)0), NULL);
#else
    p = late_string(g, agfindattr(g, "ranksep"), NULL);
#endif
    if (p) {
	if (sscanf(p, "%lf", &xf) == 0)
	    xf = DEFAULT_RANKSEP;
	else {
	    if (xf < MIN_RANKSEP)
		xf = MIN_RANKSEP;
	}
	if (strstr(p, "equally"))
	    GD_exact_ranksep(g) = TRUE;
    } else
	xf = DEFAULT_RANKSEP;
    GD_ranksep(g) = POINTS(xf);

#ifdef WITH_CGRAPH
    GD_showboxes(g) = late_int(g, agattr(g,AGRAPH, "showboxes", NULL), 0, 0);
    p = late_string(g, agattr(g, AGRAPH,"fontnames", NULL), NULL);
#else
    GD_showboxes(g) = late_int(g, agfindattr(g, "showboxes"), 0, 0);
    p = late_string(g, agfindattr(g, "fontnames"), NULL);
#endif
    GD_fontnames(g) = maptoken(p, fontnamenames, fontnamecodes);

    setRatio(g);
    GD_drawing(g)->filled =
	getdoubles2ptf(g, "size", &(GD_drawing(g)->size));
    getdoubles2ptf(g, "page", &(GD_drawing(g)->page));

    GD_drawing(g)->centered = mapbool(agget(g, "center"));

    if ((p = agget(g, "rotate")))
	GD_drawing(g)->landscape = (atoi(p) == 90);
    else if ((p = agget(g, "orientation")))
	GD_drawing(g)->landscape = ((p[0] == 'l') || (p[0] == 'L'));
    else if ((p = agget(g, "landscape")))
	GD_drawing(g)->landscape = mapbool(p);

    p = agget(g, "clusterrank");
    CL_type = maptoken(p, rankname, rankcode);
    p = agget(g, "concentrate");
    Concentrate = mapbool(p);
    State = GVBEGIN;

    GD_drawing(g)->dpi = 0.0;
    if (((p = agget(g, "dpi")) && p[0])
	|| ((p = agget(g, "resolution")) && p[0]))
	GD_drawing(g)->dpi = atof(p);

    do_graph_label(g);

    Initial_dist = MYHUGE;

    /* initialize nodes */
#ifdef WITH_CGRAPH
    N_height = agattr(g, AGNODE,"height", NULL);
    N_width = agattr(g, AGNODE, "width", NULL);
    N_shape = agattr(g, AGNODE, "shape", NULL);
    N_color = agattr(g, AGNODE, "color", NULL);
    N_fillcolor = agattr(g, AGNODE, "fillcolor", NULL);
    N_style = agattr(g, AGNODE, "style", NULL);
    N_fontsize = agattr(g, AGNODE, "fontsize", NULL);
    N_fontname = agattr(g, AGNODE, "fontname", NULL);
    N_fontcolor = agattr(g, AGNODE, "fontcolor", NULL);
    N_label = agattr(g, AGNODE, "label", NULL);
    N_showboxes = agattr(g, AGNODE, "showboxes", NULL);
    N_penwidth = agattr(g, AGNODE, "penwidth", NULL);
    /* attribs for polygon shapes */
    N_sides = agattr(g, AGNODE, "sides", NULL);
    N_peripheries = agattr(g, AGNODE, "peripheries", NULL);
    N_skew = agattr(g, AGNODE, "skew", NULL);
    N_orientation = agattr(g, AGNODE, "orientation", NULL);
    N_distortion = agattr(g, AGNODE, "distortion", NULL);
    N_fixed = agattr(g, AGNODE, "fixedsize", NULL);
    N_imagescale = agattr(g, AGNODE, "imagescale", NULL);
    N_nojustify = agattr(g, AGNODE, "nojustify", NULL);
    N_layer = agattr(g, AGNODE, "layer", NULL);
    N_group = agattr(g, AGNODE, "group", NULL);
    N_comment = agattr(g, AGNODE, "comment", NULL);
    N_vertices = agattr(g, AGNODE, "vertices", NULL);
    N_z = agattr(g, AGNODE, "z", NULL);
#else
    N_height = agfindattr(g->proto->n, "height");
    N_width = agfindattr(g->proto->n, "width");
    N_shape = agfindattr(g->proto->n, "shape");
    N_color = agfindattr(g->proto->n, "color");
    N_fillcolor = agfindattr(g->proto->n, "fillcolor");
    N_style = agfindattr(g->proto->n, "style");
    N_fontsize = agfindattr(g->proto->n, "fontsize");
    N_fontname = agfindattr(g->proto->n, "fontname");
    N_fontcolor = agfindattr(g->proto->n, "fontcolor");
    N_label = agfindattr(g->proto->n, "label");
    N_showboxes = agfindattr(g->proto->n, "showboxes");
    N_penwidth = agfindattr(g->proto->n, "penwidth");
    /* attribs for polygon shapes */
    N_sides = agfindattr(g->proto->n, "sides");
    N_peripheries = agfindattr(g->proto->n, "peripheries");
    N_skew = agfindattr(g->proto->n, "skew");
    N_orientation = agfindattr(g->proto->n, "orientation");
    N_distortion = agfindattr(g->proto->n, "distortion");
    N_fixed = agfindattr(g->proto->n, "fixedsize");
    N_imagescale = agfindattr(g->proto->n, "imagescale");
    N_nojustify = agfindattr(g->proto->n, "nojustify");
    N_layer = agfindattr(g->proto->n, "layer");
    N_group = agfindattr(g->proto->n, "group");
    N_comment = agfindattr(g->proto->n, "comment");
    N_vertices = agfindattr(g->proto->n, "vertices");
    N_z = agfindattr(g->proto->n, "z");
#endif

    /* initialize edges */
#ifdef WITH_CGRAPH
    E_weight = agattr(g, AGEDGE, "weight", NULL);
    E_color = agattr(g, AGEDGE, "color", NULL);
    E_fontsize = agattr(g, AGEDGE, "fontsize", NULL);
    E_fontname = agattr(g, AGEDGE, "fontname", NULL);
    E_fontcolor = agattr(g, AGEDGE, "fontcolor", NULL);
    E_label = agattr(g, AGEDGE, "label", NULL);
    E_label_float = agattr(g, AGEDGE, "labelfloat", NULL);
    /* vladimir */
    E_dir = agattr(g, AGEDGE, "dir", NULL);
    E_arrowhead = agattr(g, AGEDGE, "arrowhead", NULL);
    E_arrowtail = agattr(g, AGEDGE, "arrowtail", NULL);
    E_headlabel = agattr(g, AGEDGE, "headlabel", NULL);
    E_taillabel = agattr(g, AGEDGE, "taillabel", NULL);
    E_labelfontsize = agattr(g, AGEDGE, "labelfontsize", NULL);
    E_labelfontname = agattr(g, AGEDGE, "labelfontname", NULL);
    E_labelfontcolor = agattr(g, AGEDGE, "labelfontcolor", NULL);
    E_labeldistance = agattr(g, AGEDGE, "labeldistance", NULL);
    E_labelangle = agattr(g, AGEDGE, "labelangle", NULL);
    /* end vladimir */
    E_minlen = agattr(g, AGEDGE, "minlen", NULL);
    E_showboxes = agattr(g, AGEDGE, "showboxes", NULL);
    E_style = agattr(g, AGEDGE, "style", NULL);
    E_decorate = agattr(g, AGEDGE, "decorate", NULL);
    E_arrowsz = agattr(g, AGEDGE, "arrowsize", NULL);
    E_constr = agattr(g, AGEDGE, "constraint", NULL);
    E_layer = agattr(g, AGEDGE, "layer", NULL);
    E_comment = agattr(g, AGEDGE, "comment", NULL);
    E_tailclip = agattr(g, AGEDGE, "tailclip", NULL);
    E_headclip = agattr(g, AGEDGE, "headclip", NULL);
    E_penwidth = agattr(g, AGEDGE, "penwidth", NULL);
#else
    E_weight = agfindattr(g->proto->e, "weight");
    E_color = agfindattr(g->proto->e, "color");
    E_fontsize = agfindattr(g->proto->e, "fontsize");
    E_fontname = agfindattr(g->proto->e, "fontname");
    E_fontcolor = agfindattr(g->proto->e, "fontcolor");
    E_label = agfindattr(g->proto->e, "label");
    E_label_float = agfindattr(g->proto->e, "labelfloat");
    /* vladimir */
    E_dir = agfindattr(g->proto->e, "dir");
    E_arrowhead = agfindattr(g->proto->e, "arrowhead");
    E_arrowtail = agfindattr(g->proto->e, "arrowtail");
    E_headlabel = agfindattr(g->proto->e, "headlabel");
    E_taillabel = agfindattr(g->proto->e, "taillabel");
    E_labelfontsize = agfindattr(g->proto->e, "labelfontsize");
    E_labelfontname = agfindattr(g->proto->e, "labelfontname");
    E_labelfontcolor = agfindattr(g->proto->e, "labelfontcolor");
    E_labeldistance = agfindattr(g->proto->e, "labeldistance");
    E_labelangle = agfindattr(g->proto->e, "labelangle");
    /* end vladimir */
    E_minlen = agfindattr(g->proto->e, "minlen");
    E_showboxes = agfindattr(g->proto->e, "showboxes");
    E_style = agfindattr(g->proto->e, "style");
    E_decorate = agfindattr(g->proto->e, "decorate");
    E_arrowsz = agfindattr(g->proto->e, "arrowsize");
    E_constr = agfindattr(g->proto->e, "constraint");
    E_layer = agfindattr(g->proto->e, "layer");
    E_comment = agfindattr(g->proto->e, "comment");
    E_tailclip = agfindattr(g->proto->e, "tailclip");
    E_headclip = agfindattr(g->proto->e, "headclip");
    E_penwidth = agfindattr(g->proto->e, "penwidth");
#endif
}

void graph_cleanup(graph_t *g)
{
    free(GD_drawing(g));
    GD_drawing(g) = NULL;
    free_label(GD_label(g));
#ifdef WITH_CGRAPH
    //FIX HERE , STILL SHALLOW
    //memset(&(g->u), 0, sizeof(Agraphinfo_t));
    agclean(g, AGRAPH,"Agraphinfo_t");
#else
    memset(&(g->u), 0, sizeof(Agraphinfo_t));
#endif
}

/* charsetToStr:
 * Given an internal charset value, return a canonical string
 * representation.
 */
char*
charsetToStr (int c)
{
   char* s;

   switch (c) {
   case CHAR_UTF8 :
	s = "UTF-8";
	break;
   case CHAR_LATIN1 :
	s = "ISO-8859-1";
	break;
   case CHAR_BIG5 :
	s = "BIG-5";
	break;
   default :
	agerr(AGERR, "Unsupported charset value %d\n", c);
	s = "UTF-8";
	break;
   }
   return s;
}

/* do_graph_label:
 * Set characteristics of graph label if it exists.
 * 
 */
void do_graph_label(graph_t * sg)
{
    char *str, *pos, *just;
    int pos_ix;

    /* it would be nice to allow multiple graph labels in the future */
    if ((str = agget(sg, "label"))) {
	char pos_flag;
	pointf dimen;

	GD_has_labels(sg->root) |= GRAPH_LABEL;

#ifdef WITH_CGRAPH
	GD_label(sg) = make_label(agroot(sg), str, (aghtmlstr(str) ? LT_HTML : LT_NONE),
	    late_double(sg, agattr(sg,AGRAPH,"fontsize", NULL),
			DEFAULT_FONTSIZE, MIN_FONTSIZE),
	    late_nnstring(sg, agattr(sg,AGRAPH, "fontname", NULL),
			DEFAULT_FONTNAME),
	    late_nnstring(sg, agattr(sg,AGRAPH, "fontcolor", NULL),
			DEFAULT_COLOR));
#else
	GD_label(sg) = make_label((void*)sg, str, (aghtmlstr(str) ? LT_HTML : LT_NONE),
	    late_double(sg, agfindattr(sg, "fontsize"),
			DEFAULT_FONTSIZE, MIN_FONTSIZE),
	    late_nnstring(sg, agfindattr(sg, "fontname"),
			DEFAULT_FONTNAME),
	    late_nnstring(sg, agfindattr(sg, "fontcolor"),
			DEFAULT_COLOR));
#endif

	/* set label position */
	pos = agget(sg, "labelloc");
	if (sg != agroot(sg)) {
	    if (pos && (pos[0] == 'b'))
		pos_flag = LABEL_AT_BOTTOM;
	    else
		pos_flag = LABEL_AT_TOP;
	} else {
	    if (pos && (pos[0] == 't'))
		pos_flag = LABEL_AT_TOP;
	    else
		pos_flag = LABEL_AT_BOTTOM;
	}
	just = agget(sg, "labeljust");
	if (just) {
	    if (just[0] == 'l')
		pos_flag |= LABEL_AT_LEFT;
	    else if (just[0] == 'r')
		pos_flag |= LABEL_AT_RIGHT;
	}
	GD_label_pos(sg) = pos_flag;

	if (sg == agroot(sg))
	    return;

	/* Set border information for cluster labels to allow space
	 */
	dimen = GD_label(sg)->dimen;
	PAD(dimen);
	if (!GD_flip(agroot(sg))) {
	    if (GD_label_pos(sg) & LABEL_AT_TOP)
		pos_ix = TOP_IX;
	    else
		pos_ix = BOTTOM_IX;
	    GD_border(sg)[pos_ix] = dimen;
	} else {
	    /* when rotated, the labels will be restored to TOP or BOTTOM  */
	    if (GD_label_pos(sg) & LABEL_AT_TOP)
		pos_ix = RIGHT_IX;
	    else
		pos_ix = LEFT_IX;
	    GD_border(sg)[pos_ix].x = dimen.y;
	    GD_border(sg)[pos_ix].y = dimen.x;
	}
    }
}
