#include "./xhtml_parser.h"

#include "./libxml_iter.h"
#include "./xhtml_string_util.h"
#include "doc_api/token_addressing.h"

#include <libxml/parser.h>

#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <unordered_map>

#define DEBUG 0
#define DEBUG_LOG(msg) if (DEBUG) { std::cerr << std::string(context.node_depth * 2, ' ') << msg << std::endl; }

namespace {

#define EMPTY_STR ""

struct Context
{
    int node_depth = 0;     // depth inside any nodes
    int list_depth = 0;     // depth inside ul/ol nodes
    int pre_depth = 0;      // depth inside pre nodes
    DocAddr current_address;

    std::function<void(const Context&, TokenType, std::string)> _emit_token;
    std::function<void(std::string)> emit_id;

    void emit_token(TokenType type, std::string text)
    {
        _emit_token(*this, type, std::move(text));
    }

    Context(
        DocAddr start_address,
        std::function<void(const Context&, TokenType, std::string)> _emit_token,
        std::function<void(std::string)> emit_id
    ) : current_address(start_address), _emit_token(_emit_token), emit_id(emit_id)
    { }
};

bool _element_is_blocking(const xmlChar *name)
{
    if (!name)
    {
        return false;
    }
    static const std::set<std::string> blocking_elements = {
        "address", "article", "aside", "blockquote", "canvas", "dd", "div", "dl", "dt",
        "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4",
        "h5", "h6", "header", "hgroup", "hr", "li", "main", "nav", "noscript", "ol",
        "output", "p", "pre", "section", "table", "tfoot", "ul", "video", "br"
    };
    return blocking_elements.find((const char*)name) != blocking_elements.end();
}

/////////////////////////////

void _on_enter_h(int, xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
}

void _on_exit_h(int, xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
}

/////////////////////////////

void _on_enter_ul(xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
}

void _on_exit_ul(xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
}

/////////////////////////////

void _on_enter_p(xmlNodePtr, Context &context)
{
    if (context.list_depth == 0)
    {
        context.emit_token(TokenType::Section, EMPTY_STR);
    }
}

void _on_exit_p(xmlNodePtr, Context &context)
{
    if (context.list_depth == 0)
    {
        context.emit_token(TokenType::Section, EMPTY_STR);
    }
}

/////////////////////////////

void _on_enter_pre(xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
    context.pre_depth++;
}

void _on_exit_pre(xmlNodePtr, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);
    context.pre_depth--;
}

/////////////////////////////

void _on_enter_image(xmlNodePtr node, Context &context)
{
    context.emit_token(TokenType::Section, EMPTY_STR);

    // Placeholder image
    const xmlChar *img_path = xmlGetProp(node, BAD_CAST "href");
    if (!img_path) img_path = xmlGetProp(node, BAD_CAST "src");

    std::string token_text = (
        "[Image" +
        (img_path ? " " + std::filesystem::path((const char*)img_path).filename().string() : "") +
        "]"
    );

    context.emit_token(TokenType::Image, token_text);

    context.emit_token(TokenType::Section, EMPTY_STR);
}

/////////////////////////////

const std::unordered_map<std::string, std::function<void(xmlNodePtr, Context &)>> _on_enter_handlers = {
    {"h1", [](xmlNodePtr node, Context &context) {
        _on_enter_h(1, node, context);
    }},
    {"h2", [](xmlNodePtr node, Context &context) {
        _on_enter_h(2, node, context);
    }},
    {"h3", [](xmlNodePtr node, Context &context) {
        _on_enter_h(3, node, context);
    }},
    {"h4", [](xmlNodePtr node, Context &context) {
        _on_enter_h(4, node, context);
    }},
    {"h5", [](xmlNodePtr node, Context &context) {
        _on_enter_h(5, node, context);
    }},
    {"h6", [](xmlNodePtr node, Context &context) {
        _on_enter_h(6, node, context);
    }},
    {"ol", _on_enter_ul},
    {"ul", _on_enter_ul},
    {"p", _on_enter_p},
    {"pre", _on_enter_pre},
    {"image", _on_enter_image},
    {"img", _on_enter_image}
};

const std::unordered_map<std::string, std::function<void(xmlNodePtr, Context &)>> _on_exit_handlers = {
    {"h1", [](xmlNodePtr node, Context &context) {
        _on_exit_h(1, node, context);
    }},
    {"h2", [](xmlNodePtr node, Context &context) {
        _on_exit_h(2, node, context);
    }},
    {"h3", [](xmlNodePtr node, Context &context) {
        _on_exit_h(3, node, context);
    }},
    {"h4", [](xmlNodePtr node, Context &context) {
        _on_exit_h(4, node, context);
    }},
    {"h5", [](xmlNodePtr node, Context &context) {
        _on_exit_h(5, node, context);
    }},
    {"h6", [](xmlNodePtr node, Context &context) {
        _on_exit_h(6, node, context);
    }},
    {"ol", _on_exit_ul},
    {"ul", _on_exit_ul},
    {"p", _on_exit_p},
    {"pre", _on_exit_pre}
};

/////////////////////////////

void on_text_node(xmlNodePtr node, Context &context)
{
    const char *str = (const char*)node->content;
    if (str)
    {
        context.emit_token(
            TokenType::Text,
            context.pre_depth > 0
                ? remove_carriage_returns(str)
                : compact_whitespace(str)
        );
    }
}

void on_enter_element_node(xmlNodePtr node, Context &context)
{
    const xmlChar *elem_id = xmlGetProp(node, BAD_CAST "id");
    if (elem_id && xmlStrlen(elem_id) > 0)
    {
        context.emit_id((const char*)elem_id);
    }

    if (_element_is_blocking(node->name))
    {
        context.emit_token(TokenType::TextBreak, EMPTY_STR);
    }

    auto handler = _on_enter_handlers.find((const char*)node->name);
    if (handler != _on_enter_handlers.end())
    {
        handler->second(node, context);
    }
}

void on_exit_element_node(xmlNodePtr node, Context &context)
{
    if (_element_is_blocking(node->name))
    {
        context.emit_token(TokenType::TextBreak, EMPTY_STR);
    }

    auto handler = _on_exit_handlers.find((const char*)node->name);
    if (handler != _on_exit_handlers.end())
    {
        handler->second(node, context);
    }
}

void _process_node(xmlNodePtr node, Context &context)
{
    // Note: addressing scheme needs to be consistent across code revisions to ensure
    // user bookmarks don't change position.
    while (node)
    {
        DEBUG_LOG("<node name=\"" << node->name << "\">");
        context.node_depth++;

        // Enter handlers
        if (node->type == XML_TEXT_NODE)
        {
            on_text_node(node, context);
            context.current_address += get_address_width((const char*)node->content);
        }
        else if (node->type == XML_ELEMENT_NODE)
        {
            on_enter_element_node(node, context);
            if (xmlStrEqual(node->name, BAD_CAST "img") || xmlStrEqual(node->name, BAD_CAST "image"))
            {
                context.current_address++;
            }
        }

        // Descend
        _process_node(node->children, context);

        // Exit handlers
        if (node->type == XML_ELEMENT_NODE)
        {
            on_exit_element_node(node, context);
        }

        context.node_depth--;
        DEBUG_LOG("</node name=\"" << node->name << "\">");

        node = node->next;
    }
}

class TokenProcessor
{
    uint32_t chapter_number;
    std::vector<DocToken> &tokens;
    std::unordered_map<std::string, DocAddr> &id_to_addr;
    bool fresh_line = true;

    std::vector<std::string> pending_ids;
    DocAddr last_address = 0;

    void attach_pending_ids(DocAddr address)
    {
        for (const auto &id : pending_ids)
        {
            id_to_addr[id] = address;
        }
        pending_ids.clear();
    }

    void write_token(const Context &context, TokenType type, DocAddr address, const std::string &text)
    {
        tokens.emplace_back(type, address, text);
        DEBUG_LOG(to_string(tokens.back()));

        attach_pending_ids(address);
    }

public:
    TokenProcessor(
        uint32_t chapter_number,
        std::vector<DocToken> &out_tokens,
        std::unordered_map<std::string, DocAddr> &out_id_to_addr
    ) : chapter_number(chapter_number),
        tokens(out_tokens),
        id_to_addr(out_id_to_addr)
    { }

    void on_id(std::string id)
    {
        // Don't store the id to address mapping yet, wait until we are storing a token
        // to ensure it maps to a token that exists.
        pending_ids.push_back(std::move(id));
    }

    void on_token(const Context &context, TokenType type, std::string text)
    {
        DocAddr address = context.current_address;

        if (text.size())
        {
            const std::string *prev_text = (
                !tokens.empty() ? &tokens.back().text : nullptr
            );

            bool strip_left = (
                fresh_line ||
                (
                     is_whitespace(text[0]) &&
                     prev_text->size() &&
                     is_whitespace(prev_text->at(prev_text->size() - 1))
                )
            );

            if (strip_left)
            {
                text = std::string(strip_whitespace_left(text.c_str()));
            }
        }

        if (type == TokenType::Text)
        {
            if (text.size())
            {
                write_token(context, type, address, text);
                fresh_line = false;
            }
        }
        else
        {
            fresh_line = true;

            if (type == TokenType::TextBreak)
            {
                if (!tokens.empty() && tokens.back().type == TokenType::Text)
                {
                    write_token(context, type, address, EMPTY_STR);
                }
            }
            else if (type == TokenType::Section)
            {
                DocAddr adjusted_address = address;
                if (!tokens.empty() && tokens.back().type == TokenType::TextBreak)
                {
                    // There might have been ids attached to the popped token. Use the previous
                    // address when we emit the new token.
                    adjusted_address = tokens.back().address;
                    tokens.pop_back();
                    DEBUG_LOG("pop");
                }
                if (!tokens.empty() && tokens.back().type != TokenType::Section)
                {
                    write_token(context, type, adjusted_address, EMPTY_STR);
                }
            }
            else
            {
                write_token(context, type, address, text);
            }
        }
    }

    void finalize()
    {
        attach_pending_ids(last_address);
    }
};

} // namespace

bool parse_xhtml_tokens(const char *xml_str, std::string name, uint32_t chapter_number, std::vector<DocToken> &tokens_out, std::unordered_map<std::string, DocAddr> &id_to_addr_out)
{
    xmlDocPtr doc = xmlReadMemory(xml_str, strlen(xml_str), nullptr, nullptr, XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_RECOVER);
    if (doc == nullptr)
    {
        std::cerr << "Unable to parse " << name << " as xml" << std::endl;
        return false;
    }

    xmlNodePtr node = xmlDocGetRootElement(doc);

    node = elem_first_child(elem_first_by_name(node, BAD_CAST "html"));
    node = elem_first_child(elem_first_by_name(node, BAD_CAST "body"));

    TokenProcessor processor(chapter_number, tokens_out, id_to_addr_out);
    if (node)
    {
        Context context(
            make_address(chapter_number),
            [&processor](const Context &context, TokenType type, std::string text){
                processor.on_token(context, type, std::move(text));
            },
            [&processor](std::string id){
                processor.on_id(std::move(id));
            }
        );
        _process_node(node, context);
    }

    processor.finalize();
    xmlFreeDoc(doc);

    return true;
}
