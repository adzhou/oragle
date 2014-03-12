//
// CppToken.h
//
// $Id: //poco/1.4/CppParser/include/Poco/CppParser/CppToken.h#2 $
//
// Library: CppParser
// Package: CppParser
// Module:  CppToken
//
// Definition of the CppToken class.
//
// Copyright (c) 2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#ifndef CppParser_CppToken_INCLUDED
#define CppParser_CppToken_INCLUDED


#include "Poco/CppParser/CppParser.h"
#include "Poco/Token.h"
#include <map>


namespace Poco {
namespace CppParser {


class CppParser_API CppToken: public Poco::Token
	/// The base class for all C++ tokens.
{
public:
	CppToken();
	~CppToken();
	
protected:
	void syntaxError(const std::string& expected, const std::string& actual);
};


class CppParser_API OperatorToken: public CppToken
{
public:
	enum Tokens
	{
		OP_OPENBRACKET = 1, // [
		OP_CLOSBRACKET,     // ]
		OP_OPENPARENT,      // (
		OP_CLOSPARENT,      // )
		OP_OPENBRACE,       // {
		OP_CLOSBRACE,       // }
		OP_LT,              // <
		OP_LE,              // <=
		OP_SHL,             // <<
		OP_SHL_ASSIGN,		// <<=
		OP_GT,              // >
		OP_GE,              // >=
		OP_SHR,             // >>
		OP_SHR_ASSIGN,      // >>=
		OP_ASSIGN,          // =
		OP_EQ,              // ==
		OP_NOT,             // !
		OP_NE,              // !=
		OP_BITAND,          // &
		OP_BITAND_ASSIGN,   // &=
		OP_AND,             // &&
		OP_BITOR,           // |
		OP_BITOR_ASSIGN,    // |= 
		OP_OR,              // ||
		OP_XOR,             // ^
		OP_XOR_ASSIGN,      // ^=
		OP_COMPL,           // ~
		OP_ASTERISK,        // *
		OP_ASTERISK_ASSIGN, // *=
		OP_SLASH,           // /
		OP_SLASH_ASSIGN,    // /=
		OP_PLUS,            // +
		OP_PLUS_ASSIGN,     // +=
		OP_INCR,            // ++
		OP_MINUS,           // -
		OP_MINUS_ASSIGN,    // -=
		OP_DECR,            // --
		OP_ARROW,           // ->
		OP_MOD,             // %
		OP_MOD_ASSIGN,      // %=
		OP_COMMA,           // ,
		OP_PERIOD,          // .
		OP_TRIPLE_PERIOD,   // ...
		OP_COLON,           // :
		OP_DBL_COLON,       // ::
		OP_SEMICOLON,       // ;
		OP_QUESTION         // ?
	};
	
	OperatorToken();
	~OperatorToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	int asInteger() const;
	
private:
	typedef std::map<std::string, int> OpMap;
	
	OpMap _opMap;
};


class CppParser_API IdentifierToken: public CppToken
{
public:
	enum Keywords
	{
		KW_ALIGNAS = 1,
		KW_ALIGNOF,
		KW_AND,
		KW_AND_EQ,
		KW_ASM,
		KW_AUTO,
		KW_BITAND,
		KW_BITOR,
		KW_BOOL,
		KW_BREAK,
		KW_CASE,
		KW_CATCH,
		KW_CHAR,
		KW_CHAR_16T,
		KW_CHAR_32T,
		KW_CLASS,
		KW_COMPL,
		KW_CONST,
		KW_CONSTEXPR,
		KW_CONST_CAST,
		KW_CONTINUE,
		KW_DECLTYPE,
		KW_DEFAULT,
		KW_DELETE,
		KW_DO,
		KW_DOUBLE,
		KW_DYNAMIC_CAST,
		KW_ELSE,
		KW_ENUM,
		KW_EXPLICIT,
		KW_EXPORT,
		KW_EXTERN,
		KW_FALSE,
		KW_FLOAT,
		KW_FOR,
		KW_FRIEND,
		KW_GOTO,
		KW_IF,
		KW_INLINE,
		KW_INT,
		KW_LONG,
		KW_MUTABLE,
		KW_NAMESPACE,
		KW_NEW,
		KW_NOEXCEPT,
		KW_NOT,
		KW_NOT_EQ,
		KW_NULLPTR,
		KW_OPERATOR,
		KW_OR,
		KW_OR_EQ,
		KW_PRIVATE,
		KW_PROTECTED,
		KW_PUBLIC,
		KW_REGISTER,
		KW_REINTERPRET_CAST,
		KW_RETURN,
		KW_SHORT,
		KW_SIGNED,
		KW_SIZEOF,
		KW_STATIC,
		KW_STATIC_ASSERT,
		KW_STATIC_CAST,
		KW_STRUCT,
		KW_SWITCH,
		KW_TEMPLATE,
		KW_THIS,
		KW_THREAD_LOCAL,
		KW_THROW,
		KW_TRUE,
		KW_TRY,
		KW_TYPEDEF,
		KW_TYPEID,
		KW_TYPENAME,
		KW_UNION,
		KW_UNSIGNED,
		KW_USING,
		KW_VIRTUAL,
		KW_VOID,
		KW_VOLATILE,
		KW_WCHAR_T,
		KW_WHILE,
		KW_XOR,
		KW_XOR_EQ
	};
	
	IdentifierToken();
	~IdentifierToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	int asInteger() const;
	
private:
	typedef std::map<std::string, int> KWMap;
	
	KWMap _kwMap;
};


class CppParser_API StringLiteralToken: public CppToken
{
public:
	StringLiteralToken();
	~StringLiteralToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	std::string asString() const;
};


class CppParser_API CharLiteralToken: public CppToken
{
public:
	CharLiteralToken();
	~CharLiteralToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	char asChar() const;
};


class CppParser_API NumberLiteralToken: public CppToken
{
public:
	NumberLiteralToken();
	~NumberLiteralToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	int asInteger() const;
	double asFloat() const;
	
private:
	bool _isFloat;
};


class CppParser_API CommentToken: public CppToken
{
public:
	CommentToken();
	~CommentToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
	std::string asString() const;
};


class CppParser_API PreprocessorToken: public CppToken
{
public:
	PreprocessorToken();
	~PreprocessorToken();
	Poco::Token::Class tokenClass() const;
	bool start(char c, std::istream& istr);
	void finish(std::istream& istr);
};


} } // namespace Poco::CppParser


#endif // CppParser_CppToken_INCLUDED
