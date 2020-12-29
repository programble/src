/* Copyright (C) 2020  C. McEnroe <june@causal.agency>
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

%option prefix="c11"
%option noyywrap

%{
#include "hilex.h"
%}

%x MacroLine MacroInclude
%x CharLiteral StringLiteral

width "*"|[0-9]+

%%
	static int pop = INITIAL;

[[:space:]]+ { return Normal; }

^"%" {
	BEGIN(pop = MacroLine);
	return Macro;
}

([-+*/%&|^=!<>]|"<<"|">>")"="? |
[=~.?:]|"++"|"--"|"&&"|"||"|"->" |
sizeof|(_A|alignof) {
	return Operator;
}

([1-9][0-9]*|"0"[0-7]*|"0x"[[:xdigit:]]+)([ulUL]{0,3}) |
[0-9]*("."[0-9]*)?([eE][+-]?[0-9]+)?[flFL]? |
"0x"[[:xdigit:]]*("."[[:xdigit:]]*)?([pP][+-]?[0-9]+)[flFL]? {
	return Number;
}

auto|break|case|const|continue|default|do|else|enum|extern|for|goto|if|inline |
register|restrict|return|static|struct|switch|typedef|union|volatile|while |
(_A|a)lignas|_Atomic|_Generic|(_N|n)oreturn|(_S|s)tatic_assert |
(_T|t)hread_local {
	return Keyword;
}

[_[:alpha:]][_[:alnum:]]* { return Identifier; }

<INITIAL,MacroLine>"//"([^\n]|"\\\n")* |
<INITIAL,MacroLine>"/*"([^*]|"*"[^/])*"*"+"/" {
	return Comment;
}

<INITIAL,MacroLine>[LUu]?"'" {
	BEGIN(CharLiteral);
	return String;
}
<INITIAL,MacroLine,MacroInclude>([LU]|u8?)?"\"" {
	BEGIN(StringLiteral);
	return String;
}

<CharLiteral,StringLiteral>{
	"\\"[''""?\\abfnrtv] |
	"\\"([0-7]{1,3}) |
	"\\x"([[:xdigit:]]{2}) |
	"\\u"([[:xdigit:]]{4}) |
	"\\U"([[:xdigit:]]{8}) {
		return StringEscape;
	}
}
<StringLiteral>{
	"%%" |
	"%"[ #+-0]*{width}?("."{width})?([Lhjltz]|hh|ll)?[AEFGXacdefginopsux] {
		return StringFormat;
	}
}

<CharLiteral>{
	"'" {
		BEGIN(pop);
		return String;
	}
	[^\\'']+|. { return String; }
}
<StringLiteral>{
	"\"" {
		BEGIN(pop);
		return String;
	}
	[^%\\""]+|. { return String; }
}

^"#" {
	BEGIN(pop = MacroLine);
	return Macro;
}
^"#"[[:blank:]]*("include"|"import") {
	BEGIN(pop = MacroInclude);
	return Macro;
}
<MacroInclude>"<"[^>]+">" {
	BEGIN(pop = MacroLine);
	return String;
}
<MacroLine,MacroInclude>{
	"\n" {
		BEGIN(pop = INITIAL);
		return Macro;
	}
	"\\\n" { return Macro; }
	[^\\\n/<>''""]+|. { return Macro; }
}

. { return Normal; }

%{
	(void)yyunput;
	(void)input;
%}

%%

const struct Lexer LexC = { yylex, &yyin, &yytext };