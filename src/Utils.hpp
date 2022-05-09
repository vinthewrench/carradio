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

 
namespace Utils {
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

