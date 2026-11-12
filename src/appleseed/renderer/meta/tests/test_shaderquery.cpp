
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// appleseed.renderer headers.
#include "renderer/modeling/shadergroup/shaderquery.h"

// appleseed.foundation headers.
#include "foundation/containers/dictionary.h"
#include "foundation/utility/test.h"

// Standard headers.
#include <string>

using namespace foundation;
using namespace renderer;

TEST_SUITE(Renderer_Modeling_ShaderQuery)
{
    // These minimal OSO streams exercise queries without a runtime compiler
    // or filesystem fixtures. They are not executed by the shading system.
    const char FirstShader[] =
        "OpenShadingLanguage 1.00\n"
        "shader first %meta{string,help,\"first shader\"}\n"
        "param float weight 1.25 %meta{string,label,\"First weight\"}\n"
        "code ___main___\nend\n";

    const char SecondShader[] =
        "OpenShadingLanguage 1.00\n"
        "shader second %meta{string,help,\"second shader\"}\n"
        "param int count 7\n"
        "param string filename \"image.tx\"\n"
        "code ___main___\nend\n";

    TEST_CASE(RepeatedQueriesReturnCachedDictionaries)
    {
        auto query = ShaderQueryFactory::create();
        ASSERT_TRUE(query->open_bytecode(FirstShader));
        ASSERT_EQ(1, query->get_param_count());
        const Dictionary& parameter = query->get_param_info(0);
        const Dictionary& metadata = query->get_metadata();

        EXPECT_EQ("weight", std::string(parameter.get("name")));
        EXPECT_EQ("First weight", std::string(parameter.dictionaries().get("metadata")
            .dictionaries().get("label").get("value")));
        EXPECT_EQ("first shader", std::string(metadata.dictionaries().get("help").get("value")));
        EXPECT_EQ(&parameter, &query->get_param_info(0));
        EXPECT_EQ(&metadata, &query->get_metadata());
    }

    TEST_CASE(ReopeningDiscardsCachedMetadataAndParameters)
    {
        auto query = ShaderQueryFactory::create();
        ASSERT_TRUE(query->open_bytecode(FirstShader));
        query->get_param_info(0);
        query->get_metadata();

        ASSERT_TRUE(query->open_bytecode(SecondShader));
        ASSERT_EQ(2, query->get_param_count());
        EXPECT_EQ("second", std::string(query->get_shader_name()));
        EXPECT_EQ("count", std::string(query->get_param_info(0).get("name")));
        EXPECT_EQ("filename", std::string(query->get_param_info(1).get("name")));
        EXPECT_EQ("second shader", std::string(query->get_metadata()
            .dictionaries().get("help").get("value")));
    }

    TEST_CASE(FailedOpenClearsCachedStateAndAllowsRecovery)
    {
        auto query = ShaderQueryFactory::create();
        ASSERT_TRUE(query->open_bytecode(FirstShader));
        query->get_param_info(0);
        query->get_metadata();

        EXPECT_FALSE(query->open_bytecode("not a compiled shader"));
        EXPECT_EQ(0, query->get_param_count());
        EXPECT_TRUE(query->get_metadata().empty());

        ASSERT_TRUE(query->open_bytecode(SecondShader));
        ASSERT_EQ(2, query->get_param_count());
        EXPECT_EQ("count", std::string(query->get_param_info(0).get("name")));
        EXPECT_EQ("second shader", std::string(query->get_metadata()
            .dictionaries().get("help").get("value")));
    }
}
