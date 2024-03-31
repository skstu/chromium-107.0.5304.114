// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/import/csv_password_sequence.h"

#include <iterator>
#include <string>
#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace password_manager {

TEST(CSVPasswordSequenceTest, Constructions) {
  static constexpr char kCsv[] = "login,url,password\nabcd,http://goo.gl,ef";
  CSVPasswordSequence seq1(kCsv);

  EXPECT_NE(seq1.begin(), seq1.end());

  CSVPasswordSequence seq2(std::move(seq1));
  EXPECT_NE(seq2.begin(), seq2.end());
}

TEST(CSVPasswordSequenceTest, HeaderOnly) {
  static constexpr char kHeader[] =
      "Display Name,Login,Secret Question,Password,URL,Timestamp";
  CSVPasswordSequence seq(kHeader);
  EXPECT_EQ(CSVPassword::Status::kOK, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, AllowSpacesInHeaderField) {
  static constexpr char kHeader[] =
      " Display Name ,  Login,Secret Question ,  Password,  URL,  Timestamp ";
  CSVPasswordSequence seq(kHeader);
  EXPECT_EQ(CSVPassword::Status::kOK, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, MissingColumns) {
  static constexpr char kNoUrlCol[] =
      "Display Name,Login,Secret Question,Password,x,Timestamp\n"
      ":),Bob,ABCD!,odd,https://example.org,132\n";
  CSVPasswordSequence seq(kNoUrlCol);
  EXPECT_EQ(CSVPassword::Status::kSemanticError, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, DuplicatedColumns) {
  // Leave out URL but use both "UserName" and "Login". That way the username
  // column is duplicated while the overall number of interesting columns
  // matches the number of labels.
  static constexpr char kBothUsernameAndLogin[] =
      "UserName,Login,Secret Question,Password,Timestamp\n"
      ":),Bob,ABCD!,odd,132\n";
  CSVPasswordSequence seq(kBothUsernameAndLogin);
  EXPECT_EQ(CSVPassword::Status::kSemanticError, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, Empty) {
  CSVPasswordSequence seq((std::string()));
  EXPECT_EQ(CSVPassword::Status::kSyntaxError, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, InvalidCSVHeader) {
  static constexpr char kQuotes[] =
      "Display Name,Login,Secret Question,Password,URL,Timestamp,\"\n"
      ":),Bob,ABCD!,odd,https://example.org,132\n";
  CSVPasswordSequence seq(kQuotes);
  EXPECT_EQ(CSVPassword::Status::kSyntaxError, seq.result());
  EXPECT_EQ(seq.begin(), seq.end());
}

TEST(CSVPasswordSequenceTest, SkipsEmptyLines) {
  static constexpr char kNoUrl[] =
      "Display Name,Login,Secret Question,Password,URL,Timestamp\n"
      "\n"
      "\t\t\t\r\n"
      "            \n"
      "non_empty,pwd\n"
      "non_empty,pwd\n"
      "    ";
  CSVPasswordSequence seq(kNoUrl);
  EXPECT_EQ(CSVPassword::Status::kOK, seq.result());
  ASSERT_EQ(2, std::distance(seq.begin(), seq.end()));
}

TEST(CSVPasswordSequenceTest, Iteration) {
  static constexpr char kCsv[] =
      "Display Name,,Login,Secret Question,Password,URL,Timestamp\n"
      "DN,value-of-an-empty-named-column,user,?,pwd,http://example.com,123\n"
      ",<,Alice,123?,even,https://example.net,213,past header count = ignored\n"
      ":),,Bob,ABCD!,odd,https://example.org,132\n";
  constexpr struct {
    base::StringPiece url;
    base::StringPiece username;
    base::StringPiece password;
  } kExpectedCredentials[] = {
      {"http://example.com", "user", "pwd"},
      {"https://example.net", "Alice", "even"},
      {"https://example.org", "Bob", "odd"},
  };
  CSVPasswordSequence seq(kCsv);
  EXPECT_EQ(CSVPassword::Status::kOK, seq.result());

  size_t order = 0;
  for (const CSVPassword& pwd : seq) {
    ASSERT_LT(order, std::size(kExpectedCredentials));
    const auto& expected = kExpectedCredentials[order];
    EXPECT_EQ(GURL(expected.url), pwd.GetURL());
    EXPECT_EQ(expected.username, pwd.GetUsername());
    EXPECT_EQ(expected.password, pwd.GetPassword());
    ++order;
  }
}

TEST(CSVPasswordSequenceTest, MissingEolAtEof) {
  static constexpr char kCsv[] =
      "url,login,password\n"
      "http://a.com,l,p";
  CSVPasswordSequence seq(kCsv);
  EXPECT_EQ(CSVPassword::Status::kOK, seq.result());

  ASSERT_EQ(1, std::distance(seq.begin(), seq.end()));
  CSVPassword pwd = *seq.begin();
  EXPECT_EQ(GURL("http://a.com"), pwd.GetURL());
  EXPECT_EQ("l", pwd.GetUsername());
  EXPECT_EQ("p", pwd.GetPassword());
}

}  // namespace password_manager
