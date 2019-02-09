/* vim: set foldmethod=marker foldlevel=0: */
/* Copyright (C) 2019  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <err.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <unistd.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

typedef unsigned Set;
#define SET(x) ((Set)1 << (x))

#define ENUM_CLASS \
	X(Normal)  \
	X(Keyword) \
	X(Macro)   \
	X(String)  \
	X(Escape)  \
	X(Format)  \
	X(Interp)  \
	X(Comment) \
	X(Todo)

enum Class {
#define X(class) class,
	ENUM_CLASS
#undef X
	ClassLen,
};

struct Syntax {
	enum Class class;
	Set parent;
	size_t subexp;
	const char *pattern;
	const char *pattend;
};

#define WB "(^|[^_[:alnum:]]|\n)"
#define WS "[[:blank:]]*"
#define PATTERN_SQ "'([^']|[\\]')*'"
#define PATTERN_DQ "\"([^\"]|[\\]\")*\""
#define PATTERN_TODO "FIXME|TODO|XXX"

// C syntax {{{
static const struct Syntax CSyntax[] = {
	{ Keyword, .subexp = 2, .pattern = WB
		"(" "auto|extern|register|(_T|t)hread_local|typedef"
		"|" "_Atomic|const|restrict|volatile"
		"|" "inline|(_N|n)oreturn"
		"|" "(_A|a)lignas"
		"|" "enum|struct|union"
		"|" "do|else|for|if|switch|while"
		"|" "break|case|continue|default|goto|return"
		")" WB },
	{ Macro,
		.pattern = "^" WS "#(.|[\\]\n)*" },
	{ String, .parent = SET(Macro), .subexp = 1,
		.pattern = "include" WS "(<[^>]*>)" },
	{ String,
		.pattern = "[LUu]?" PATTERN_SQ },
	{ String,
		.pattern = "([LU]|u8?)?" PATTERN_DQ },
	{ Escape, .parent = SET(String),
		.pattern = "[\\]([\"'?\\abfnrtv]|[0-7]{1,3}|x[0-9A-Fa-f]+)" },
	{ Escape, .parent = SET(String),
		.pattern = "[\\](U[0-9A-Fa-f]{8}|u[0-9A-Fa-f]{4})" },
	{ Format, .parent = SET(String), .pattern =
		"%%|%[ #+-0]*"         // flags
		"([*]|[0-9]+)?"        // field width
		"([.]([*]|[0-9]+))?"   // precision
		"([Lhjltz]|hh|ll)?"    // length modifier
		"[AEFGXacdefginopsux]" // format specifier
	},
	{ Comment, .parent = ~SET(String),
		.pattern = "//.*" },
	{ Comment, .parent = ~SET(String),
		.pattern = "/[*]",
		.pattend = "[*]/" },
	{ Todo, .parent = SET(Comment),
		.pattern = PATTERN_TODO },
};
// }}}

// make syntax {{{
static const struct Syntax MakeSyntax[] = {
	{ Keyword, .subexp = 2,
		.pattern = WB "([.](PHONY|PRECIOUS|SUFFIXES))" WB },
	{ Macro,
		.pattern = "^ *-?include" },
	{ String, .subexp = 1,
		.pattern = "[._[:alnum:]]+" WS "[!+:?]?=" WS "(.*)" },
	{ Normal,
		.pattern = "^\t.*" },
	{ String,
		.pattern = PATTERN_SQ },
	{ String,
		.pattern = PATTERN_DQ },
	{ Interp,
		.pattern = "[$]." },
	// Support one level of nesting with the same delimiter.
	{ Interp,
		.pattern = "[$][(](" "[^$)]" "|" "[$][(][^)]*[)]" ")*[)]" },
	{ Interp,
		.pattern = "[$][{](" "[^$}]" "|" "[$][{][^}]*[}]" ")*[}]" },
	{ Escape,
		.pattern = "[$][$]" },
	{ Comment,
		.pattern = "#.*" },
	{ Todo, .parent = SET(Comment),
		.pattern = PATTERN_TODO },
};
// }}}

// mdoc syntax {{{
static const struct Syntax MdocSyntax[] = {
	{ Keyword, .subexp = 2, .pattern = WB
		"(" "D[dt]|N[dm]|Os"
		"|" "S[hsx]|[LP]p|Xr"
		"|" "%[ABCDIJNOPQRTUV]|[BE][dl]|D[1l]|It|Ql|R[es]|Ta"
		"|" "Ap|[BE]k|Ns|Pf|Sm"
		"|" "Ar|Cm|Ev|Fl|O[cop]|Pa"
		"|" "Dv|Er|F[acdnot]|In|Lb|V[at]"
		"|" "A[dn]|Cd|Lk|M[st]"
		"|" "[BE]f|Em|Li|No|Sy"
		"|" "(Br|[ABDPQS])[coq]|E[co]"
		"|" "At|(Bs|[BDEFNO])x|Rv|St"
		")" WB },
	{ String,
		.pattern = PATTERN_DQ },
	{ Normal,
		.pattern = "^[^.].*" },
	{ String,
		.pattern = "[\\](" "." "|" "[(].{2}" "|" "[[][^]]*[]]" ")" },
	{ Comment,
		.pattern = "^[.][\\]\".*" },
	{ Todo, .parent = SET(Comment),
		.pattern = PATTERN_TODO },
};
// }}}

// sh syntax {{{
static const struct Syntax ShSyntax[] = {
	{ Keyword, .subexp = 2, .pattern = WB
		"(" "!|case|do|done|elif|else|esac|fi|for|if|in|then|until|while"
		"|" "alias|bg|cd|command|false|fc|fg|getopts|jobs|kill|newgrp|pwd|read"
		"|" "true|umask|unalias|wait"
		"|" "[.:]|break|continue|eval|exec|exit|export|local|readonly|return"
		"|" "set|shift|times|trap|unset"
		")" WB },
	{ String,
		.pattern = PATTERN_DQ },
	{ String, .subexp = 1,
		.pattern = "<<-?" WS "EOF.*(\n)",
		.pattend = "^\t*EOF$" },
	{ Escape, .parent = SET(String),
		.pattern = "[\\][\"$\\`]" },
	{ String, .parent = ~SET(Escape),
		.pattern = "[\\]." },
	{ Interp, .parent = ~SET(Escape),
		.pattern = "[$]([!#$*?@-]|[_[:alnum:]]+)" },
	{ Interp, .parent = ~SET(Escape),
		.pattern = "[$][{][^}]*[}]" },
	{ Interp, .parent = ~SET(Escape),
		.pattern = "[$][(][^)]*[)]" "|" "`[^`]*`" },
	{ String,
		.pattern = "'[^']*'" },
	{ String, .subexp = 1,
		.pattern = "<<-?" WS "'EOF'.*(\n)",
		.pattend = "^\t*EOF$" },
	{ Normal, .parent = SET(String),
		.pattern = "^\t*EOF$" },
	{ Comment, .parent = ~SET(String), .subexp = 2,
		.pattern = "(^|" WS ")" "(#.*)" },
	{ Todo, .parent = SET(Comment),
		.pattern = PATTERN_TODO },
};
// }}}

static const struct Language {
	const char *name;
	const char *pattern;
	const struct Syntax *syntax;
	size_t len;
} Languages[] = {
	{ "c",    "[.][ch]$", CSyntax, ARRAY_LEN(CSyntax) },
	{ "make", "[.]mk$|^Makefile$", MakeSyntax, ARRAY_LEN(MakeSyntax) },
	{ "mdoc", "[.][1-9]$", MdocSyntax, ARRAY_LEN(MdocSyntax) },
	{ "sh",   "[.]sh$", ShSyntax, ARRAY_LEN(ShSyntax) },
};

static regex_t compile(const char *pattern, int flags) {
	regex_t regex;
	int error = regcomp(&regex, pattern, REG_EXTENDED | REG_NEWLINE | flags);
	if (!error) return regex;
	char buf[256];
	regerror(error, &regex, buf, sizeof(buf));
	errx(EX_SOFTWARE, "regcomp: %s: %s", buf, pattern);
}

enum { SubsLen = 8 };
static void highlight(struct Language lang, enum Class *hi, const char *str) {
	for (size_t i = 0; i < lang.len; ++i) {
		struct Syntax syn = lang.syntax[i];
		regex_t regex = compile(syn.pattern, 0);
		regex_t regend = {0};
		if (syn.pattend) regend = compile(syn.pattend, 0);

		assert(syn.subexp < SubsLen);
		assert(syn.subexp <= regex.re_nsub);
		regmatch_t subs[SubsLen] = {{0}};
		for (size_t offset = 0; str[offset]; offset += subs[syn.subexp].rm_eo) {
			int error = regexec(
				&regex, &str[offset], SubsLen, subs, offset ? REG_NOTBOL : 0
			);
			if (error == REG_NOMATCH) break;
			if (error) errx(EX_SOFTWARE, "regexec: %d", error);

			if (lang.syntax[i].pattend) {
				regmatch_t end;
				error = regexec(
					&regend, &str[offset + subs[syn.subexp].rm_eo],
					1, &end, REG_NOTBOL
				);
				if (error == REG_NOMATCH) break;
				if (error) errx(EX_SOFTWARE, "regexec: %d", error);
				subs[syn.subexp].rm_eo += end.rm_eo;
			}

			regmatch_t sub = subs[syn.subexp];
			if (syn.parent) {
				if (~syn.parent & SET(hi[offset + sub.rm_so])) continue;
			}
			for (regoff_t j = sub.rm_so; j < sub.rm_eo; ++j) {
				hi[offset + j] = lang.syntax[i].class;
			}
		}
		regfree(&regex);
		if (lang.syntax[i].pattend) regfree(&regend);
	}
}

static void check(void) {
	for (size_t i = 0; i < ARRAY_LEN(Languages); ++i) {
		regex_t regex = compile(Languages[i].pattern, REG_NOSUB);
		regfree(&regex);
		for (size_t j = 0; j < Languages[i].len; ++j) {
			struct Syntax syn = Languages[i].syntax[j];
			regex = compile(syn.pattern, 0);
			if (syn.subexp >= SubsLen || syn.subexp > regex.re_nsub) {
				errx(
					EX_SOFTWARE, "subexpression %zu out of bounds: %s",
					syn.subexp, syn.pattern
				);
			}
			regfree(&regex);
			if (syn.pattend) {
				regex = compile(syn.pattend, REG_NOSUB);
				regfree(&regex);
			}
		}
	}
}

#define ENUM_OPTION \
	X(Monospace, "monospace") \
	X(Document, "document") \
	X(Title, "title") \
	X(CSS, "css") \
	X(Inline, "inline")

enum Option {
#define X(option, _) option,
	ENUM_OPTION
#undef X
	OptionLen,
};

static const char *OptionKey[OptionLen] = {
#define X(option, key) [option] = key,
	ENUM_OPTION
#undef X
};

typedef void HeaderFn(const char *opts[]);
typedef void
OutputFn(const char *opts[], enum Class class, const char *str, size_t len);

// ANSI format {{{

enum SGR {
	SGRBoldOn = 1,
	SGRBoldOff = 22,
	SGRBlack = 30,
	SGRRed,
	SGRGreen,
	SGRYellow,
	SGRBlue,
	SGRMagenta,
	SGRCyan,
	SGRWhite,
	SGRDefault = 39,
};

static const enum SGR ANSIStyle[ClassLen][3] = {
	[Normal]  = { SGRDefault },
	[Keyword] = { SGRWhite },
	[Macro]   = { SGRGreen },
	[String]  = { SGRCyan },
	[Escape]  = { SGRDefault },
	[Format]  = { SGRCyan, SGRBoldOn, SGRBoldOff },
	[Interp]  = { SGRGreen },
	[Comment] = { SGRBlue },
	[Todo]    = { SGRBlue, SGRBoldOn, SGRBoldOff },
};

static void
ansiOutput(const char *opts[], enum Class class, const char *str, size_t len) {
	(void)opts;
	// Style each line separately, otherwise less -R won't look right.
	while (len) {
		size_t line = strcspn(str, "\n");
		if (line > len) line = len;
		if (ANSIStyle[class][1]) {
			printf(
				"\x1B[%d;%dm%.*s\x1B[%dm",
				ANSIStyle[class][0], ANSIStyle[class][1],
				(int)line, str,
				ANSIStyle[class][2]
			);
		} else {
			printf("\x1B[%dm%.*s", ANSIStyle[class][0], (int)line, str);
		}
		if (line < len) {
			printf("\n");
			line++;
		}
		str += line;
		len -= line;
	}
}

// }}}

// IRC format {{{

enum IRC {
	IRCWhite,
	IRCBlack,
	IRCBlue,
	IRCGreen,
	IRCRed,
	IRCBrown,
	IRCMagenta,
	IRCOrange,
	IRCYellow,
	IRCLightGreen,
	IRCCyan,
	IRCLightCyan,
	IRCLightBlue,
	IRCPink,
	IRCGray,
	IRCLightGray,
	IRCBold = 0x02,
	IRCColor = 0x03,
	IRCMonospace = 0x11,
};

static const enum IRC SGRIRC[] = {
	[SGRBoldOn]  = IRCBold,
	[SGRBoldOff] = IRCBold,
	[SGRBlack]   = IRCBlack,
	[SGRRed]     = IRCRed,
	[SGRGreen]   = IRCGreen,
	[SGRYellow]  = IRCYellow,
	[SGRBlue]    = IRCBlue,
	[SGRMagenta] = IRCMagenta,
	[SGRCyan]    = IRCCyan,
	[SGRWhite]   = IRCGray,
	[SGRDefault] = 0,
};

static void ircHeader(const char *opts[]) {
	if (opts[Monospace]) printf("%c", IRCMonospace);
}

static void
ircOutput(const char *opts[], enum Class class, const char *str, size_t len) {
	char cc[3] = "";
	if (ANSIStyle[class][0] != SGRDefault) {
		snprintf(cc, sizeof(cc), "%d", SGRIRC[ANSIStyle[class][0]]);
	}
	// Style each line separately, for multiple IRC messages.
	while (len) {
		size_t line = strcspn(str, "\n");
		if (line > len) line = len;
		if (ANSIStyle[class][1]) {
			printf(
				"%c%s%c%.*s%c",
				IRCColor, cc, SGRIRC[ANSIStyle[class][1]],
				(int)line, str,
				SGRIRC[ANSIStyle[class][2]]
			);
		} else {
			// Double-toggle bold to prevent str being interpreted as color.
			printf(
				"%c%s%c%c%.*s",
				IRCColor, cc, IRCBold, IRCBold, (int)line, str
			);
		}
		if (line < len) {
			printf("\n");
			line++;
			if (opts[Monospace] && line < len) printf("%c", IRCMonospace);
		}
		str += line;
		len -= line;
	}
}

// }}}

// HTML format {{{

static void htmlEscape(const char *str, size_t len) {
	while (len) {
		size_t run = strcspn(str, "&<>");
		if (run > len) run = len;
		switch (str[0]) {
			break; case '"': run = 1; printf("&quot;");
			break; case '&': run = 1; printf("&amp;");
			break; case '<': run = 1; printf("&lt;");
			break; case '>': run = 1; printf("&gt;");
			break; default:  printf("%.*s", (int)run, str);
		}
		str += run;
		len -= run;
	}
}

static const char *ClassName[ClassLen] = {
#define X(class) [class] = #class,
	ENUM_CLASS
#undef X
};

static const char *HTMLStyle[ClassLen] = {
	[Keyword]  = "color: dimgray;",
	[Macro]    = "color: green;",
	[String]   = "color: teal;",
	[Format]   = "color: teal; font-weight: bold;",
	[Interp]   = "color: green;",
	[Comment]  = "color: navy;",
	[Todo]     = "color: navy; font-weight: bold;",
};

static void htmlHeader(const char *opts[]) {
	if (!opts[Document]) goto pre;
	printf("<!DOCTYPE html>\n<title>");
	if (opts[Title]) htmlEscape(opts[Title], strlen(opts[Title]));
	printf("</title>\n");
	if (opts[CSS]) {
		printf("<link rel=\"stylesheet\" href=\"");
		htmlEscape(opts[CSS], strlen(opts[CSS]));
		printf("\">\n");
	} else if (!opts[Inline]) {
		printf("<style>\n");
		for (enum Class class = 0; class < ClassLen; ++class) {
			if (!HTMLStyle[class]) continue;
			printf(".hi.%s { %s }\n", ClassName[class], HTMLStyle[class]);
		}
		printf("</style>\n");
	}
pre:
	printf("<pre class=\"hi\">");
}

static void htmlFooter(const char *opts[]) {
	(void)opts;
	printf("</pre>\n");
}

static void
htmlOutput(const char *opts[], enum Class class, const char *str, size_t len) {
	if (opts[Inline]) {
		printf("<span style=\"%s\">", HTMLStyle[class] ? HTMLStyle[class] : "");
	} else {
		printf("<span class=\"hi %s\">", ClassName[class]);
	}
	htmlEscape(str, len);
	printf("</span>");
}

// }}}

static const struct Format {
	const char *name;
	OutputFn *output;
	HeaderFn *header;
	HeaderFn *footer;
} Formats[] = {
	{ "ansi", ansiOutput, NULL, NULL },
	{ "irc",  ircOutput, ircHeader, NULL },
	{ "html", htmlOutput, htmlHeader, htmlFooter },
};

static bool findLanguage(struct Language *lang, const char *name) {
	for (size_t i = 0; i < ARRAY_LEN(Languages); ++i) {
		if (strcmp(name, Languages[i].name)) continue;
		*lang = Languages[i];
		return true;
	}
	return false;
}

static bool matchLanguage(struct Language *lang, const char *name) {
	for (size_t i = 0; i < ARRAY_LEN(Languages); ++i) {
		regex_t regex = compile(Languages[i].pattern, REG_NOSUB);
		int error = regexec(&regex, name, 0, NULL, 0);
		regfree(&regex);
		if (error == REG_NOMATCH) continue;
		if (error) errx(EX_SOFTWARE, "regexec: %d", error);
		*lang = Languages[i];
		return true;
	}
	return false;
}

static bool findFormat(struct Format *format, const char *name) {
	for (size_t i = 0; i < ARRAY_LEN(Formats); ++i) {
		if (strcmp(name, Formats[i].name)) continue;
		*format = Formats[i];
		return true;
	}
	return false;
}

static bool findOption(enum Option *opt, const char *key) {
	for (*opt = 0; *opt < OptionLen; ++*opt) {
		if (!strcmp(key, OptionKey[*opt])) return true;
	}
	return false;
}

int main(int argc, char *argv[]) {
	const char *name = NULL;
	struct Language lang = {0};
	struct Format format = Formats[0];
	const char *opts[OptionLen] = {0};

	int opt;
	while (0 < (opt = getopt(argc, argv, "cf:l:n:o:"))) {
		switch (opt) {
			break; case 'c': check(); return EX_OK;
			break; case 'f': {
				if (!findFormat(&format, optarg)) {
					errx(EX_USAGE, "no such format %s", optarg);
				}
			}
			break; case 'l': {
				if (!findLanguage(&lang, optarg)) {
					errx(EX_USAGE, "no such language %s", optarg);
				}
			}
			break; case 'n': name = optarg;
			break; case 'o': {
				enum Option key;
				char *keystr, *valstr;
				while (NULL != (valstr = strsep(&optarg, ","))) {
					keystr = strsep(&valstr, "=");
					if (!findOption(&key, keystr)) {
						errx(EX_USAGE, "no such option %s", keystr);
					}
					opts[key] = (valstr ? valstr : keystr);
				}
			}
			break; default: return EX_USAGE;
		}
	}

	const char *path = "(stdin)";
	FILE *file = stdin;
	if (optind < argc) {
		path = argv[optind];
		file = fopen(path, "r");
		if (!file) err(EX_NOINPUT, "%s", path);
	}

	if (!name) {
		name = strrchr(path, '/');
		name = (name ? &name[1] : path);
	}
	if (!lang.syntax && !matchLanguage(&lang, name)) {
		errx(EX_USAGE, "cannot infer language for %s", name);
	}
	if (!opts[Title]) opts[Title] = name;

	size_t len = 32 * 1024;
	if (file != stdin) {
		struct stat stat;
		int error = fstat(fileno(file), &stat);
		if (error) err(EX_IOERR, "fstat");
		len = stat.st_size;
	}

	char *str = malloc(len + 1);
	if (!str) err(EX_OSERR, "malloc");

	len = fread(str, 1, len, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	str[len] = '\0';

	enum Class *hi = calloc(len, sizeof(*hi));
	if (!hi) err(EX_OSERR, "calloc");

	highlight(lang, hi, str);

	if (format.header) format.header(opts);
	size_t run = 0;
	for (size_t i = 0; i < len; i += run) {
		for (run = 0; i + run < len; ++run) {
			if (hi[i + run] != hi[i]) break;
		}
		format.output(opts, hi[i], &str[i], run);
	}
	if (format.footer) format.footer(opts);
}