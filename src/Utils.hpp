// ============================================================================
//    Author: Kenneth Perkins
//    Date:   Dec 10, 2020
//    Taken From: http://programmingnotes.org/
//    File:  Utils.h
//    Description: Handles general utility functions
// ============================================================================
#pragma once
#include <string>
#include <algorithm>
#include <cctype>
#include <vector>
#include <sstream>
#include <iomanip>
#include <map>
#include "tinyutf8.h"

namespace Utils {



static std::map <tiny_utf8::string, std::string> foreign_characters = {
	{ "äæǽ", "ae" },
	{ "öœ", "oe" },
	{ "ü", "u" },
	{ "Ä", "A" },
	{ "Ü", "U" },
	{ "Ö", "O" },
	{ "ÀÁÂÃÄÅǺĀĂĄǍΑΆẢẠẦẪẨẬẰẮẴẲẶА", "A" },
	{ "àáâãåǻāăąǎªαάảạầấẫẩậằắẵẳặа", "a" },
	{ "Б", "B" },
	{ "б", "b" },
	{ "ÇĆĈĊČ", "C" },
	{ "çćĉċč", "c" },
	{ "Д", "D" },
	{ "д", "d" },
	{ "ÐĎĐΔ", "Dj" },
	{ "ðďđδ", "dj" },
	{ "ÈÉÊËĒĔĖĘĚΕΈẼẺẸỀẾỄỂỆЕЭ", "E" },
	{ "èéêëēĕėęěέεẽẻẹềếễểệеэ", "e" },
	{ "Ф", "F" },
	{ "ф", "f" },
	{ "ĜĞĠĢΓГҐ", "G" },
	{ "ĝğġģγгґ", "g" },
	{ "ĤĦ", "H" },
	{ "ĥħ", "h" },
	{ "ÌÍÎÏĨĪĬǏĮİΗΉΊΙΪỈỊИЫ", "I" },
	{ "ìíîïĩīĭǐįıηήίιϊỉịиыї", "i" },
	{ "Ĵ", "J" },
	{ "ĵ", "j" },
	{ "ĶΚК", "K" },
	{ "ķκк", "k" },
	{ "ĹĻĽĿŁΛЛ", "L" },
	{ "ĺļľŀłλл", "l" },
	{ "М", "M" },
	{ "м", "m" },
	{ "ÑŃŅŇΝН", "N" },
	{ "ñńņňŉνн", "n" },
	{ "ÒÓÔÕŌŎǑŐƠØǾΟΌΩΏỎỌỒỐỖỔỘỜỚỠỞỢО", "O" },
	{ "òóôõōŏǒőơøǿºοόωώỏọồốỗổộờớỡởợо", "o" },
	{ "П", "P" },
	{ "п", "p" },
	{ "ŔŖŘΡР", "R" },
	{ "ŕŗřρр", "r" },
	{ "ŚŜŞȘŠΣС", "S" },
	{ "śŝşșšſσςс", "s" },
	{ "ȚŢŤŦτТ", "T" },
	{ "țţťŧт", "t" },
	{ "ÙÚÛŨŪŬŮŰŲƯǓǕǗǙǛŨỦỤỪỨỮỬỰУ", "U" },
	{ "ùúûũūŭůűųưǔǖǘǚǜυύϋủụừứữửựу", "u" },
	{ "ÝŸŶΥΎΫỲỸỶỴЙ", "Y" },
	{ "ýÿŷỳỹỷỵй", "y" },
	{ "В", "V" },
	{ "в", "v" },
	{ "Ŵ", "W" },
	{ "ŵ", "w" },
	{ "ŹŻŽΖЗ", "Z" },
	{ "źżžζз", "z" },
	{ "ÆǼ", "AE" },
	{ "ß", "ss" },
	{ "Ĳ", "IJ" },
	{ "ĳ", "ij" },
	{ "Œ", "OE" },
	{ "ƒ", "f" },
	{ "ξ", "ks" },
	{ "π", "p" },
	{ "β", "v" },
	{ "μ", "m" },
	{ "ψ", "ps" },
	{ "Ё", "Yo" },
	{ "ё", "yo" },
	{ "Є", "Ye" },
	{ "є", "ye" },
	{ "Ї", "Yi" },
	{ "Ж", "Zh" },
	{ "ж", "zh" },
	{ "Х", "Kh" },
	{ "х", "kh" },
	{ "Ц", "Ts" },
	{ "ц", "ts" },
	{ "Ч", "Ch" },
	{ "ч", "ch" },
	{ "Ш", "Sh" },
	{ "ш", "sh" },
	{ "Щ", "Shch" },
	{ "щ", "shch" },
	{ "ЪъЬь", "" },
	{ "Ю", "Yu" },
	{ "ю", "yu" },
	{ "Я", "Ya" },
	{ "я", "ya" },
};
 
inline bool is_ascii (std::string strIn){
	for(int i = 0; i < strIn.size(); i++){
		if(!isascii(strIn[i])) return false;
	}
	return true;
	
}

inline std::string removeDiacritics(std::string strIn){
	
	if (strIn.empty() || is_ascii(strIn))
		return strIn;
	
	tiny_utf8::string s1  = tiny_utf8::string(strIn);
	
	for( auto &[key, replacement]: foreign_characters){
		for (auto codepoint : key) {
			for(int i = 0; i < s1.length(); i++){
				if(codepoint == s1[i]){
					s1 = s1.replace(i,1 , replacement );
					i += replacement.size();
				}
			}
		}
	}
	
	return std::string(s1.data(),s1.size());
}
 
// invariant: line_sz > length of any single word
inline std::vector<std::string> split( std::string str, std::size_t line_sz )
{
	 if( std::all_of( std::begin(str), std::end(str), [] ( char c ) { return std::isspace(c) ; } ) )
		  return {} ; // empty string or string with all spaces, return an empty vector

	 std::vector<std::string> result(1) ; // vector containing one empty string

	 std::istringstream stm(str) ; // use a string stream to split on white-space
	 std::string word ;
	 while( stm >> word ) // for each word in the string
	 {
		  // if this word will fit into the current line, append the word to the current line
		  if( ( result.back().size() + word.size() ) <= line_sz ) result.back() += word + ' ' ;

		  else
		  {
				result.back().pop_back() ; // remove the trailing space at the end of the current line
				result.push_back( word + ' ' ) ; // and place this new word on the next line
		  }
	 }

	 result.back().pop_back() ; // remove the trailing space at the end of the last line
	 return result ;
}

inline  std::string trimCNTRL(std::string source) {
	  source.erase(std::find_if(source.rbegin(), source.rend(), [](char c) {
			return !std::iscntrl(static_cast<unsigned char>(c));
	  }).base(), source.end());
	  return source;
 }


	 /**
	 * FUNCTION: trimEnd
	 * USE: Returns a new string with all trailing whitespace characters removed
	 * @param source =  The source string
	 * @return: A new string with all the trailing whitespace characters removed
	 */
	inline  std::string trimEnd(std::string source) {
		  source.erase(std::find_if(source.rbegin(), source.rend(), [](char c) {
				return !std::isspace(static_cast<unsigned char>(c));
		  }).base(), source.end());
		  return source;
	 }

	 /**
	 * FUNCTION: trimStart
	 * USE: Returns a new string with all leading whitespace characters removed
	 * @param source - The source string
	 * @return: A new string with all the leading whitespace characters removed
	 */
	inline  std::string trimStart(std::string source) {
		  source.erase(source.begin(), std::find_if(source.begin(), source.end(), [](char c) {
				return !std::isspace(static_cast<unsigned char>(c));
		  }));
		  return source;
	 }

	 /**
	 * FUNCTION: trim
	 * USE: Returns a new string with all the leading and trailing whitespace
	 *   characters removed
	 * @param source  - The source string
	 * @return: A new string with all the leading and trailing whitespace
	 *   characters removed
	 */
	 inline std::string trim(std::string source) {
		  return trimEnd(trimStart(source));
	 }
}// http://programmingnotes.org/

template<typename T>
std::vector<T>
split(const T & str, const T & delimiters) {
	std::vector<T> v;
	 typename T::size_type start = 0;
	 auto pos = str.find_first_of(delimiters, start);
	 while(pos != T::npos) {
		  if(pos != start) // ignore empty tokens
				v.emplace_back(str, start, pos - start);
		  start = pos + 1;
		  pos = str.find_first_of(delimiters, start);
	 }
	 if(start < str.length()) // ignore trailing delimiter
		  v.emplace_back(str, start, str.length() - start); // add what's left of the string
	 return v;
}

/**
  * FUNCTION: replaceAll
  * USE: Replaces all occurrences of the 'oldValue' string with the
  *   'newValue' string
  * @param source The source string
  * @param oldValue The string to be replaced
  * @param newValue The string to replace all occurrences of oldValue
  * @return: A new string with all occurrences replaced
  */
inline  std::string replaceAll(const std::string& source
		, const std::string& oldValue, const std::string& newValue) {
		if (oldValue.empty()) {
			 return source;
		}
		std::string newString;
		newString.reserve(source.length());
		std::size_t lastPos = 0;
		std::size_t findPos;
		while (std::string::npos != (findPos = source.find(oldValue, lastPos))) {
			 newString.append(source, lastPos, findPos - lastPos);
			 newString += newValue;
			 lastPos = findPos + oldValue.length();
		}
		newString += source.substr(lastPos);
		return newString;
  }

inline bool caseInSensStringCompare(std::string str1, std::string str2)
{
	return ((str1.size() == str2.size())
			  && std::equal(str1.begin(), str1.end(), str2.begin(), [](char & c1, char & c2){
		return (c1 == c2 || std::toupper(c1) == std::toupper(c2));
	}));
}
//
//template <typename T> inline std::string int_to_hex(T val, size_t width=sizeof(T)*2)
//{
//	std::stringstream ss;
//	ss << std::setfill('0') << std::setw(width) << std::hex << (val|0);
//	return ss.str();
//}


template <class T, class T2 = typename std::enable_if<std::is_integral<T>::value>::type>
static std::string to_hex(const T & data, bool addPrefix = false);
 
/*
template<class T, class>
inline std::string to_hex(const T & data, bool addPrefix)
{
	 std::stringstream sstream;
	 sstream << std::hex;
	 std::string ret;
	 if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || sizeof(T)==1)
	 {
		  sstream << static_cast<int>(data);
		  ret = sstream.str();
		  if (ret.length() > 2)
		  {
				ret = ret.substr(ret.length() - 2, 2);
		  }
	 }
	 else
	 {
		  sstream << data;
		  ret = sstream.str();
	 }
	 return (addPrefix ? u8"0x" : u8"") + ret;
}
*/

template<class T, class> std::string to_hex(const T & data, bool addPrefix)
{
	std::stringstream sstream;
	sstream << std::hex;
	std::string ret;
	if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || sizeof(T)==1)
	{
		sstream << std::setw(sizeof(T) * 2) << std::setfill('0') <<  static_cast<int>(data);
		 ret = sstream.str();
	}
	else
	{
	  sstream << std::setw(sizeof(T) * 2) << std::setfill('0') << data;
	  ret = sstream.str();
  }

	return (addPrefix ? u8"0x" : u8"") + ret;
}

inline std::string hexStr(unsigned char *data, int len)
{
	constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
										'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  std::string s(len * 2, ' ');
  for (int i = 0; i < len; ++i) {
	 s[2 * i]     = hexmap[(data[i] & 0xF0) >> 4];
	 s[2 * i + 1] = hexmap[data[i] & 0x0F];
  }
  return s;
}



inline std::string truncate(std::string str, size_t width, bool show_ellipsis=false)
{
	if (str.length() > width){
		 if (show_ellipsis){
				return str.substr(0, width) + "...";
		 }
		 else {
				return str.substr(0, width);
		 }
	}
	 return str;
}

template<typename TK, typename TV>
std::vector<TK> all_keys(std::map<TK, TV> const& input_map) {
  std::vector<TK> retval;
  for (auto const& element : input_map) {
	 retval.push_back(element.first);
  }
  return retval;
}

template<typename TK, typename TV>
std::vector<TV> all_values(std::map<TK, TV> const& input_map) {
  std::vector<TV> retval;
  for (auto const& element : input_map) {
	 retval.push_back(element.second);
  }
  return retval;
}
