#include "everything.h"

//#define FORCE_ACCUMULATOR // Use accumulator even if framebuffers are supported.
//#define FORCE_FRAMEBUFFER // Use framebuffers, halt if not supported.

#include <complex>
#include <iostream>
#include <list>

Events::AutoErrorHandlers error_handlers;

Window win("TAU++", ivec2(800,600), Window::Settings{}.GlVersion(2,1).GlProfile(Window::any_profile).Resizable().MinSize(ivec2(640,480)));
Timing::TickStabilizer tick_stabilizer(60);

Renderers::Poly2D r;
Input::Mouse mouse;

Graphics::Texture tex_main(Graphics::Texture::linear);
Graphics::CharMap font_main;
Graphics::CharMap font_small;
Graphics::Font font_object_main;
Graphics::Font font_object_small;


constexpr int interface_rect_height = 128;


namespace Draw
{
    inline namespace TextPresets
    {
        [[nodiscard]] auto WithCursor(int index = Input::TextCursorPos(), fvec3 color = fvec3(tick_stabilizer.ticks % 60 < 30)) // Returns a preset
        {
            return [=](Renderers::Poly2D::Text_t &obj)
            {
                obj.callback([=](Renderers::Poly2D::Text_t::CallbackParams params)
                {
                    constexpr int width = 1;
                    if (params.render_pass && params.index == index)
                    {
                        r.Quad(params.pos - ivec2(width, params.obj.state().ch_map->Ascent()), ivec2(1, params.obj.state().ch_map->Height()))
                         .color(color).alpha(params.render[0].alpha).beta(params.render[0].beta);
                    }
                });
            };
        }
        [[nodiscard]] auto ColorAfterPos(int index, fvec3 color) // Returns a preset
        {
            return [=](Renderers::Poly2D::Text_t &obj)
            {
                obj.callback([=](Renderers::Poly2D::Text_t::CallbackParams params)
                {
                    if (params.render_pass && params.index >= index)
                        for (auto &it : params.render)
                                it.color = color;
                });
            };
        }
    }

    namespace Accumulator
    {
        bool use_framebuffer = 0;
        Graphics::FrameBuffer framebuffer;
        Graphics::RenderBuffer framebuffer_rbuf;

        void Overwrite()
        {
            if (!use_framebuffer)
            {
                glAccum(GL_LOAD, 1);
            }
            else
            {
                framebuffer.Bind();
                glBlitFramebuffer(0, 0, win.Size().x, win.Size().y, 0, 0, win.Size().x, win.Size().y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                framebuffer.Unbind();
            }
        }
        void Return()
        {
            if (!use_framebuffer)
            {
                glAccum(GL_RETURN, 1);
            }
            else
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer.GetHandle());
                glBlitFramebuffer(0, 0, win.Size().x, win.Size().y, 0, 0, win.Size().x, win.Size().y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
        }
    }

    ivec2 min, max;

    void HandleResize()
    {
        min = -win.Size() / 2;
        max = win.Size() + min;

        r.SetMatrix(fmat4::ortho(ivec2(min.x, max.y), ivec2(max.x, min.y), -1, 1));
        mouse.Transform(win.Size()/2, 1);

        if (Accumulator::use_framebuffer)
            Accumulator::framebuffer_rbuf.Storage(win.Size(), GL_RGBA8);
    }

    void Init()
    {
        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();

        Graphics::Image texture_image_main("assets/texture.png");

        font_object_main.Create("assets/Xolonium-Regular.ttf", 20);
        font_object_small.Create("assets/Xolonium-Regular.ttf", 11);

        Graphics::Font::MakeAtlas(texture_image_main, ivec2(0,1024-128), ivec2(1024,128),
        {
            {font_object_main , font_main , Graphics::Font::normal, Strings::Encodings::cp1251()},
            {font_object_small, font_small, Graphics::Font::normal, Strings::Encodings::cp1251()},
        });

        tex_main.SetData(texture_image_main);

        r.Create(0x1000);
        r.SetTexture(tex_main);
        r.SetDefaultFont(font_main);
        r.BindShader();

        #if defined(FORCE_ACCUMULATOR) && defined(FORCE_FRAMEBUFFER)
        #  error You cant define both FORCE_ACCUMULATOR and FORCE_FRAMEBUFFER
        #endif

        #ifdef FORCE_ACCUMULATOR
        Accumulator::use_framebuffer = 0;
        #else
        Accumulator::use_framebuffer = bool(glBlitFramebuffer);
        #ifdef FORCE_FRAMEBUFFER
        if (!Accumulator::use_framebuffer)
            Program::Error("FORCE_FRAMEBUFFER was defined, but framebuffers are not supported on this machine.");
        #endif
        #endif
        if (Accumulator::use_framebuffer)
        {
            Accumulator::framebuffer_rbuf.Create();
            Accumulator::framebuffer_rbuf.Storage(win.Size(), GL_RGBA8);
            Accumulator::framebuffer.Create();
            Accumulator::framebuffer.Attach(Accumulator::framebuffer_rbuf);
        }

        Draw::HandleResize();
    }

    void Dot(int type, fvec2 pos, fvec3 color, float alpha = 1, float beta = 1)
    {
        int y = (type >= 2);
        if (type >= 2)
            type -= 2;
        r.Quad(pos, ivec2(13)).tex(ivec2(2+type*16,2+y*16)).center().color(color).mix(0).alpha(alpha).beta(beta);
    }
    void Line(int type, fvec2 a, fvec2 b, fvec3 color, float alpha = 1, float beta = 1)
    {
        int len = iround((b - a).len());

        for (int i = 0; i <= len; i++)
            Dot(type, a + (b - a) * i / len, color, alpha, beta);
    }
}


class Expression
{
  public:
    using complex_t = std::complex<long double>;

    struct Exception : std::exception
    {
        std::string message;
        int pos;

        Exception() {}
        Exception(std::string message, int pos) : message(message), pos(pos) {}

        const char *what() const noexcept override
        {
            return message.c_str();
        }
    };

  private:
    struct Token
    {
        enum Type {num, lparen, rparen, op, var, imag_unit};
        enum Operator {plus, minus, mul, fake_mul, div, pow, left_paren};

        Type type;

        union
        {
            Operator op_type;
            long double num_value;
        };

        int starts_at;

        std::string ToString() const
        {
            switch (type)
            {
              case num:
                return std::to_string(num_value);
              case lparen:
                return "(";
              case rparen:
                return ")";
              case var:
                return "x";
              case imag_unit:
                return "j";
              case op:
                switch (op_type)
                {
                    case plus:       return "+";
                    case minus:      return "-";
                    case mul:        return "*";
                    case fake_mul:   return "* (fake)";
                    case div:        return "/";
                    case pow:        return "^";
                    case left_paren: return "(";
                }
            }
            return "?";
        }
    };

    static int Precedence(Token::Operator op)
    {
        switch (op)
        {
            case Token::plus:       return 1;
            case Token::minus:      return 1;
            case Token::mul:        return 2;
            case Token::div:        return 2;
            case Token::pow:        return 3;
            case Token::fake_mul:   return 3;
            case Token::left_paren: return -1;
        }
        return -1;
    }
    static bool IsRightAssociative(Token::Operator op)
    {
        return op == Token::pow || op == Token::fake_mul;
    }
    static complex_t (*OperatorFunc(Token::Operator op))(complex_t, complex_t)
    {
        switch (op)
        {
          case Token::plus:
            return [](complex_t a, complex_t b){return a + b;};
          case Token::minus:
            return [](complex_t a, complex_t b){return a - b;};
          case Token::mul:
          case Token::fake_mul:
            return [](complex_t a, complex_t b){return a * b;};
          case Token::div:
            return [](complex_t a, complex_t b){return a / b;};
          case Token::pow:
            return [](complex_t a, complex_t b){return std::pow(a, b);};
          case Token::left_paren:
            return 0;
        }
        return 0;
    }

    static bool Tokenize(std::string_view str, char var_name, std::list<Token> *list, int *error_pos, std::string *error_msg)
    {
        DebugAssert("`j` is reserved for the imaginary unit.", var_name != 'j');

        std::list<Token> ret;
        Token token;

        int index = 0;
        while (1)
        {
            token.starts_at = index;
            switch (char ch = str[index])
            {
              case '\0':
                *list = ret;
                return 1;
              case '(':
                token.type = Token::lparen;
                ret.push_back(token);
                index++;
                break;
              case ')':
                token.type = Token::rparen;
                ret.push_back(token);
                index++;
                break;
              case '+':
                token.type = Token::op;
                token.op_type = Token::plus;
                ret.push_back(token);
                index++;
                break;
              case '-':
                token.type = Token::op;
                token.op_type = Token::minus;
                ret.push_back(token);
                index++;
                break;
              case '*':
                token.type = Token::op;
                token.op_type = Token::mul;
                ret.push_back(token);
                index++;
                break;
              case '/':
                token.type = Token::op;
                token.op_type = Token::div;
                ret.push_back(token);
                index++;
                break;
              case '^':
                token.type = Token::op;
                token.op_type = Token::pow;
                ret.push_back(token);
                index++;
                break;
              default:
                if ((unsigned char)ch <= ' ')
                {
                    index++;
                    break;
                }
                if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ',')
                {
                    std::string num;
                    while (1)
                    {
                        ch = str[index];
                        if (ch < '0' || ch > '9')
                            break;
                        num += ch;
                        index++;
                    }
                    ch = str[index];
                    if (ch == '.' || ch == ',')
                    {
                        index++;
                        num += '.';
                        while (1)
                        {
                            ch = str[index];
                            if (ch < '0' || ch > '9')
                            {
                                if (ch == '.' || ch == ',')
                                {
                                    *error_pos = index;
                                    *error_msg = Str("Больше одной точки в записи числа.");
                                    return 0;
                                }
                                break;
                            }
                            num += ch;
                            index++;
                        }
                    }

                    if (num == ".")
                    {
                        *error_pos = token.starts_at;
                        *error_msg = Str("Ожидались цифры до и/или после точки.");
                        return 0;
                    }

                    long double value;
                    if (auto end = Reflection::from_string(value, num.c_str()); end == num.c_str() + num.size())
                    {
                        token.type = Token::num;
                        token.num_value = value;
                        ret.push_back(token);
                        break;
                    }
                }
                if (ch == var_name)
                {
                    token.type = Token::var;
                    ret.push_back(token);
                    index++;
                    break;
                }
                if (ch == 'j')
                {
                    token.type = Token::imag_unit;
                    ret.push_back(token);
                    index++;
                    break;
                }

                *error_pos = index;
                *error_msg = Str("Ожидалось число, переменная `", var_name, "`, мнимая единица `j`, скобка или операция.");
                return 0;
            }
        }
    }

    static bool FinalizeTokenList(std::list<Token> &list, int *error_pos, std::string *error_msg)
    {
        std::vector<int> paren_stack;

        auto it = list.begin(), prev = it;

        while (it != list.end())
        {
            bool increment_iter = 1;
            switch (it->type)
            {
              case Token::lparen:
                paren_stack.push_back(it->starts_at);
                [[fallthrough]];
              case Token::num:
              case Token::var:
              case Token::imag_unit:
                if (it != prev)
                {
                    if (it != prev && prev->type != Token::lparen && prev->type != Token::op)
                    {
                        if (it->type == Token::lparen)
                            paren_stack.pop_back();
                        Token new_token;
                        new_token.type = Token::op;
                        new_token.op_type = (it->type != Token::num ? Token::mul : Token::pow);
                        new_token.starts_at = it->starts_at;
                        it = list.insert(it, new_token);
                        /*
                        *error_pos = it->starts_at;
                        *error_msg = "Пропущена операция.";
                        return 0;
                        */
                    }
                }
                break;
              case Token::rparen:
                if (paren_stack.empty())
                {
                    *error_pos = it->starts_at;
                    *error_msg = "Лишняя закрывающая скобка.";
                    return 0;
                }
                paren_stack.pop_back();
                if (it != prev && (prev->type == Token::lparen || prev->type == Token::op))
                {
                    *error_pos = it->starts_at;
                    *error_msg = "Пропущено число, переменная или мнимая единица `j`.";
                    return 0;
                }
                break;
              case Token::op:
                if ((it == prev || prev->type == Token::lparen || prev->type == Token::op) && (it->op_type == Token::plus || it->op_type == Token::minus))
                {
                    if (it->op_type == Token::minus)
                    {
                        it->op_type = Token::fake_mul;
                        Token new_token;
                        new_token.starts_at = it->starts_at;
                        new_token.type = Token::num;
                        new_token.num_value = -1;
                        it = list.insert(it, new_token);
                    }
                    else
                    {
                        it = list.erase(it);
                        increment_iter = 0;
                    }
                    break;
                }
                if (it == prev || prev->type == Token::op || prev->type == Token::lparen)
                {
                    *error_pos = it->starts_at;
                    *error_msg = "Пропущено число или переменная.";
                    return 0;
                }
                break;
            }

            prev = it;
            if (increment_iter)
                it++;
        }

        if (list.empty())
        {
            *error_pos = 0;
            *error_msg = "Пустое выражение.";
            return 0;
        }

        if (list.back().type == Token::op)
        {
            *error_pos = list.back().starts_at;
            *error_msg = "Пропущено число или переменная.";
            return 0;
        }

        if (paren_stack.size() > 0)
        {
            *error_pos = paren_stack.back();
            *error_msg = "Скобка не закрыта.";
            return 0;
        }

        return 1;
    }


    struct Element
    {
        enum Type {num, var, op};

        Type type;

        union
        {
            complex_t (*op_func)(complex_t, complex_t);
            struct
            {
                long double real, imag;
            }
            num_value;
        };
    };

    std::vector<Element> elements;


    static bool ParseExpression(const std::list<Token> &tokens, std::vector<Element> *elems)
    {
        // Shunting-yard algorithm

        std::vector<Token::Operator> op_stack;

        for (const auto &token : tokens)
        {
            switch (token.type)
            {
              case Token::num:
                {
                    Element el;
                    el.type = Element::num;
                    el.num_value.real = token.num_value;
                    el.num_value.imag = 0;
                    elems->push_back(el);
                }
                break;
              case Token::imag_unit:
                {
                    Element el;
                    el.type = Element::num;
                    el.num_value.real = 0;
                    el.num_value.imag = 1;
                    elems->push_back(el);
                }
                break;
              case Token::var:
                {
                    Element el;
                    el.type = Element::var;
                    elems->push_back(el);
                }
                break;
              case Token::op:
                {
                    int this_prec = Precedence(token.op_type);
                    Element el;
                    el.type = Element::op;
                    while (op_stack.size() > 0 && op_stack.back() != Token::left_paren && (this_prec < Precedence(op_stack.back()) || (this_prec == Precedence(op_stack.back()) && !IsRightAssociative(token.op_type))))
                    {
                        el.op_func = OperatorFunc(op_stack.back());
                        elems->push_back(el);
                        op_stack.pop_back();
                    }
                    op_stack.push_back(token.op_type);
                }
                break;
              case Token::lparen:
                op_stack.push_back(Token::left_paren);
                break;
              case Token::rparen:
                while (1)
                {
                    if (op_stack.empty())
                        return 0;

                    bool lparen_found = (op_stack.back() == Token::left_paren);
                    if (!lparen_found)
                    {
                        Element el;
                        el.type = Element::op;
                        el.op_func = OperatorFunc(op_stack.back());
                        elems->push_back(el);
                    }
                    op_stack.pop_back();

                    if (lparen_found)
                        break;
                }
                break;
            }
        }

        while (op_stack.size() > 0)
        {
            if (op_stack.back() == Token::left_paren)
                return 0;
            Element el;
            el.type = Element::op;
            el.op_func = OperatorFunc(op_stack.back());
            elems->push_back(el);
            op_stack.pop_back();
        }

        return 1;
    }

  public:
    Expression() {}
    Expression(std::string str, char var = 's')
    {
        std::list<Expression::Token> tokens;
        int err_pos;
        std::string err_msg;

        if (Tokenize(str, var, &tokens, &err_pos, &err_msg) &&
            FinalizeTokenList(tokens, &err_pos, &err_msg))
        {
            if (!ParseExpression(tokens, &elements))
                throw Exception("Недопустимое выражение.", 0);
        }
        else
        {
            throw Exception(err_msg, err_pos);
        }
    }

    explicit operator bool() const
    {
        return elements.size() > 0;
    }

    complex_t Eval(complex_t variable) const
    {
        std::vector<complex_t> stack;
        for (const auto &elem : elements)
        {
            switch (elem.type)
            {
              case Element::num:
                stack.push_back({elem.num_value.real, elem.num_value.imag});
                break;
              case Element::var:
                stack.push_back(variable);
                break;
              case Element::op:
                {
                    if (stack.size() < 2)
                        throw std::runtime_error("Ошибка при вычислении.");
                    complex_t result = elem.op_func(stack[stack.size()-2], stack.back());
                    stack.pop_back();
                    stack.pop_back(); // Sic! We pop twice.
                    stack.push_back(result);
                }
                break;
            }
        }

        if (stack.size() != 1)
            throw std::runtime_error("Ошибка при вычислении.");

        return stack.front();
    }
    ldvec2 EvalVec(complex_t variable) const
    {
        auto val = Eval(variable);
        return {val.real(), val.imag()};
    }
};


class Plot
{
  public:
    using func_t = std::function<ldvec2(long double)>;
    constexpr static long double default_scale_factor = 100,
                                 default_min = 0,
                                 default_max = 8;

    struct Func
    {
        func_t func;
        fvec3 color;
    };

  private:
    static constexpr float window_margin = 0.4;
    static constexpr int window_smallest_pix_margin = 8;

    static constexpr int bounding_box_segment_count = 512,
                         grid_max_number_precision = 6;

    static constexpr ivec2 min_grid_cell_pixel_size = ivec2(48),
                           grid_number_offset_h     = ivec2(8,0),
                           grid_number_offset_v     = ivec2(3,-8);

    static constexpr fvec3 grid_text_color = fvec3(0),
                           grid_light_text_color = fvec3(0.4);
    static constexpr float grid_zero_line_color  = 0.2,
                           grid_large_line_color = 0.55,
                           grid_mid_line_color   = 0.8,
                           grid_small_line_color = 0.85;


    using distr_t = std::uniform_real_distribution<long double>;

    std::vector<Func> funcs;
    int flags;

    ldvec2 offset = ivec2(0);
    ldvec2 scale = ivec2(default_scale_factor);
    ldvec2 default_offset = ivec2(0);
    ldvec2 default_scale = ldvec2(default_scale_factor);
    long double range_start = default_min, range_len = default_max - default_min;
    long double current_value_offset = 0.5; // NOTE: These default values must be synced with those in `ResetAccumulator()`.
    unsigned long long current_value_index = 0, current_value_max_index = 1; // ^

    ldvec2 grid_scale_step_factor = ldvec2(10);
    ivec2 grid_cell_segments = ivec2(10);
    ivec2 grid_cell_highlight_step = ivec2(5);

    bool grabbed = 0;
    ivec2 grab_offset = ivec2(0);

    bool scale_changed_this_tick = 0,
         scale_changed_prev_tick = 1;


    struct PointData
    {
        bool valid;
        ldvec2 pos;
    };

    PointData Point(long double freq, int func_index) const
    {
        PointData ret;
        ret.pos = funcs[func_index].func(freq);
        ret.valid = std::isfinite(ret.pos.x) && std::isfinite(ret.pos.y);
        ret.pos.y = -ret.pos.y;
        return ret;
    }

    static ivec2 ViewportPos()
    {
        return ivec2(0, interface_rect_height);
    }
    static ivec2 ViewportSize()
    {
        return win.Size().sub_y(interface_rect_height);
    }

    ldvec2 MinScale()
    {
        return std::pow(0.1, grid_max_number_precision - 2) * ldvec2(min_grid_cell_pixel_size * 2 + 2);
    }
    ldvec2 MaxScale()
    {
        return std::pow(10, grid_max_number_precision - 2) * ldvec2(min_grid_cell_pixel_size * 2 - 2);
    }

    void AddPoint(int type, long double value)
    {
        for (int i = 0; i < int(funcs.size()); i++)
        {
            auto point = Point(value, i);
            if (!point.valid)
                continue;
            ldvec2 pos = (point.pos + offset) * scale;
            if ((abs(pos) > win.Size()/2).any())
                continue;
            Draw::Dot(type, pos, funcs[i].color);
        }
    }


  public:
    enum Flags {lock_scale_ratio = 1};

    Plot(int flags = 0) : flags(flags)
    {
        default_offset += ViewportPos() / default_scale / 2;
        offset = default_offset;
    }
    Plot(const std::vector<Func> &funcs, long double a, long double b, int flags) : funcs(funcs), flags(flags), range_start(a), range_len(b - a)
    {
        RecalculateDefaultOffsetAndScale();
        offset = default_offset;
        scale = default_scale;
    }

    explicit operator bool() const
    {
        return funcs.size() > 0;
    }

    void Tick(int count)
    {
        // Move if needed
        if (grabbed)
        {
            if (mouse.left.up())
                grabbed = 0;

            offset = (mouse.pos() - grab_offset) / scale;

            ResetAccumulator();
        }

        if (funcs.size() > 0)
        {
            // Draw points
            while (count-- > 0)
            {
                long double value = range_start + (current_value_offset + current_value_index++ / double(current_value_max_index)) * range_len;

                if (current_value_index >= current_value_max_index)
                {
                    if (current_value_max_index == 1)
                    {
                        AddPoint(3, range_start);
                        AddPoint(2, range_start+range_len);
                    }
                    current_value_index = 0;
                    current_value_max_index <<= 1;
                    current_value_offset /= 2;
                }

                AddPoint(grabbed || scale_changed_this_tick, value);
            }
            r.Finish();
        }

        if (!scale_changed_this_tick && scale_changed_prev_tick)
            ResetAccumulator();
        scale_changed_prev_tick = scale_changed_this_tick;
        scale_changed_this_tick = 0;
    }

    void RecalculateDefaultOffsetAndScale()
    {
        if (!bool(*this))
            return;

        ldvec2 box_min(0), box_max(0);

        for (int i = 0; i < int(funcs.size()); i++)
        for (int j = 0; j <= bounding_box_segment_count; j++)
        {
            auto point = Point(j / double(bounding_box_segment_count) * range_len + range_start, i);
            if (!point.valid)
                continue;

            for (auto mem : {&ldvec2::x, &ldvec2::y})
            {
                if (point.pos.*mem < box_min.*mem)
                    box_min.*mem = point.pos.*mem;
                else if (point.pos.*mem > box_max.*mem)
                    box_max.*mem = point.pos.*mem;
            }
        }

        for (auto mem : {&ldvec2::x, &ldvec2::y})
        {
            default_offset.*mem = (box_min.*mem + box_max.*mem) / -2;
            if (box_min.*mem == box_max.*mem)
                continue;
            default_scale.*mem = (ldvec2(ViewportSize()).*mem * (1-window_margin)) / (box_max.*mem - box_min.*mem);
        }

        if (flags & lock_scale_ratio)
            default_scale = ldvec2(default_scale.min());

        clamp_assign(default_scale, MinScale(), MaxScale());

        default_offset += ViewportPos() / default_scale / 2;

        if ((abs(default_offset * default_scale) > win.Size()/2).any())
            default_offset = clamp(default_offset * default_scale, -win.Size()/2+window_smallest_pix_margin, win.Size()/2-window_smallest_pix_margin) / default_scale;
    }

    void ResetAccumulator()
    {
        Graphics::Clear(Graphics::color);

        { // Draw grid
            ldvec2 visible_size = win.Size() / scale;
            ldvec2 corner = -offset - visible_size/2;

            ldvec2 min_cell_size = min_grid_cell_pixel_size / scale;
            ldvec2 cell_size;

            for (auto mem : {&ldvec2::x, &ldvec2::y})
                cell_size.*mem = std::pow(grid_scale_step_factor.*mem, ceil(std::log(min_cell_size.*mem) / std::log(grid_scale_step_factor.*mem)));

            ivec2 line_count = iround(ceil(visible_size / cell_size)) + 1;

            ldvec2 first_cell_pos = floor(corner / cell_size) * cell_size;

            auto lambda = [&](bool text, long double ldvec2::*ld_a, long double ldvec2::*ld_b, int ivec2::*int_a, int ivec2::*int_b)
            {
                (void)ld_b;

                bool vertical = ld_a == &ldvec2::x;
                bool text_on_mid_lines = cell_size.*ld_a > min_cell_size.*ld_a * 2;

                for (int i = 0; i < line_count.*int_a * grid_cell_segments.*int_a; i++)
                {
                    long double value = first_cell_pos.*ld_a + i * cell_size.*ld_a / grid_cell_segments.*int_a;

                    bool mid_line = i % grid_cell_highlight_step.*int_a == 0;
                    bool large_line = i % grid_cell_segments.*int_a == 0;
                    bool zero_line = large_line && abs(value) < cell_size.*ld_a / grid_cell_segments.*int_a / 2;

                    if (!text)
                    { // Line

                        ldvec2 pixel_pos;
                        pixel_pos.*ld_a = (value + offset.*ld_a) * scale.*ld_a;
                        pixel_pos.*ld_b = 0;

                        ivec2 pixel_size = ivec2(5, win.Size().*int_b + 5);

                        float color;
                        if (zero_line)
                            color = grid_zero_line_color;
                        else if (large_line)
                            color = grid_large_line_color;
                        else if (mid_line)
                            color = grid_mid_line_color;
                        else
                            color = grid_small_line_color;

                        auto quad = r.Quad(pixel_pos, pixel_size).tex(ivec2(34 + 8 * !mid_line, 2), ivec2(5)).center().color(fvec3(1)).mix(1-color);

                        if (!vertical)
                            quad.matrix(fmat2(0,1,-1,0));
                    }
                    else
                    { // Number
                        if (!(large_line || (mid_line && text_on_mid_lines)))
                            continue;

                        ivec2 pixel_pos;
                        pixel_pos.*int_a = iround((value + offset.*ld_a) * scale.*ld_a);
                        pixel_pos.*int_b = (vertical ? Draw::max.*int_b : Draw::min.*int_b);
                        pixel_pos += (vertical ? grid_number_offset_v : grid_number_offset_h);

                        if (!vertical)
                            value = -value;

                        char string_buf[64] = "0";
                        if (!zero_line)
                            std::snprintf(string_buf, sizeof string_buf, "%.*Lg", grid_max_number_precision, value);

                        r.Text(pixel_pos, string_buf).align(ivec2(-1,1)).font(font_small).color(large_line ? grid_text_color : grid_light_text_color);
                    }
                }
            };

            Graphics::Blending::FuncAdd();
            Graphics::Blending::Equation(Graphics::Blending::eq_min);
            lambda(0, &ldvec2::x, &ldvec2::y, &ivec2::x, &ivec2::y);
            lambda(0, &ldvec2::y, &ldvec2::x, &ivec2::y, &ivec2::x);
            if (flags & lock_scale_ratio)
            {
                for (float angle : {f_pi/4, f_pi*3/4})
                {
                    ldvec2 n = ldvec2(cos(angle), sin(angle)),
                           n2 = ldvec2(n.y, -n.x);

                    ldvec2 pos = offset * scale;
                    pos -= pos /dot/ n2 * n2;

                    r.Quad(pos, ivec2(5,win.Size().max()*2)).tex(ivec2(34, 2), ivec2(5)).center().color(fvec3(1)).mix(1-grid_large_line_color).matrix(fmat2(n.x,-n.y,n.y,n.x));
                }
            }
            r.Finish();

            Graphics::Blending::FuncNormalPre();
            Graphics::Blending::Equation(Graphics::Blending::eq_add);
            lambda(1, &ldvec2::x, &ldvec2::y, &ivec2::x, &ivec2::y);
            lambda(1, &ldvec2::y, &ldvec2::x, &ivec2::y, &ivec2::x);
            r.Finish();
        }

        Draw::Accumulator::Overwrite();

        current_value_offset = 0.5;
        current_value_index = 0;
        current_value_max_index = 1;
    }

    void Grab()
    {
        grabbed = 1;
        grab_offset = mouse.pos() - offset * scale;
    }

    void Scale(ldvec2 scale_factor)
    {
        ldvec2 old_mid_offset = ViewportPos() / 2 / scale - offset;

        scale *= scale_factor;
        clamp_assign(scale, MinScale(), MaxScale());

        if (flags & lock_scale_ratio)
            scale.x = scale.y;

        ldvec2 new_mid_offset = ViewportPos() / 2 / scale - offset;
        offset += new_mid_offset - old_mid_offset;

        scale_changed_this_tick = 1;

        ResetAccumulator();
    }

    void ResetOffsetAndScale()
    {
        offset = default_offset;
        scale = default_scale;

        scale_changed_this_tick = 1;

        ResetAccumulator();
    }

    void SetGridScaleStepFactor(ldvec2 value)
    {
        grid_scale_step_factor = value;
    }
    void SetGridCellSegments(ivec2 value)
    {
        grid_cell_segments = value;
    }
};


class Button
{
    inline static std::string current_tooltip;

    ivec2 location = ivec2(0);
    ivec2 pos = ivec2(0);
    int icon_index = 0;
    bool visible = 0;
    bool holdable = 0;
    std::string tooltip;
    std::function<void(void)> func;

    bool pressed = 0, hovered = 0;

    ivec2 ScreenPos() const
    {
        return win.Size() * location / 2 + pos;
    }

  public:
    Button() {}
    Button(ivec2 location, ivec2 pos, int icon_index, bool holdable, std::string tooltip, std::function<void(void)> func)
        : location(location), pos(pos), icon_index(icon_index), visible(1), holdable(holdable), tooltip(tooltip), func(std::move(func)) {}

    void Tick(bool &button_pressed)
    {
        constexpr int half_extent = 24;

        if (!visible)
        {
            pressed = 0;
            hovered = 0;
            return;
        }

        ivec2 screen_pos = ScreenPos();
        hovered = (mouse.pos() >= screen_pos - half_extent).all() && (mouse.pos() < screen_pos + half_extent).all();

        if (hovered)
            current_tooltip = tooltip;

        if (hovered && button_pressed)
        {
            button_pressed = 0;
            pressed = 1;
            if (!holdable)
                func();
        }

        if (pressed && holdable)
            func();

        if (!hovered || !mouse.left.down())
            pressed = 0;
    }
    void Render() const
    {
        constexpr int pressed_offset = 2;

        if (!visible)
            return;

        ivec2 screen_pos = ScreenPos();

        // Button itself
        r.Quad(screen_pos.add_y(pressed * pressed_offset), ivec2(48)).tex(ivec2(48*hovered, 32)).center();
        // Icon
        r.Quad(screen_pos.add_y(pressed * pressed_offset), ivec2(48)).tex(ivec2(48*icon_index, 80)).center();
    }

    static const std::string &CurrentTooltip()
    {
        return current_tooltip;
    }
    static void ResetTooltip()
    {
        current_tooltip = "";
    }
};


class TextField
{
    static constexpr int height = 24, text_offset_x = 6;

    static constexpr fvec3 title_color = fvec3(0),
                           invalid_color = fvec3(0.9,0.2,0);

    using tick_func_t = std::function<void(TextField &, bool updated)>;
    using render_func_t = std::function<void(const TextField &)>;

    inline static unsigned int id_counter = 0, active_id = -1;

    unsigned int id = -2;
  public:

    ivec2 location = ivec2(0);
    ivec2 pos = ivec2(0);
    int width = 128;
    int max_chars = 64;
    std::string title;
    std::string allowed_chars = "";
    bool visible = 0;

    tick_func_t tick_func;
    render_func_t render_func;

    std::string value;

    TextField() {}
    TextField(ivec2 location, ivec2 pos, int width, int max_chars, std::string title, std::string allowed_chars = "", tick_func_t tick_func = 0, render_func_t render_func = 0)
        : id(id_counter++), location(location), pos(pos), width(width), max_chars(max_chars), title(title), allowed_chars(allowed_chars), visible(1), tick_func(tick_func), render_func(render_func) {}

    bool invalid = 0;
    int invalid_pos = 0;
    std::string invalid_text;

    ivec2 ScreenPos() const
    {
        return win.Size() * location / 2 + pos;
    }
    ivec2 ScreenSize() const
    {
        return ivec2(width, height);
    }

    void Activate()
    {
        active_id = id;
        Input::Text(0);
    }
    static void Deactivate()
    {
        active_id = -1;
        Input::Text(0);
    }

    void Tick(bool &button_pressed)
    {
        if (!visible)
            return;

        ivec2 screen_pos = ScreenPos(),
              screen_size = ScreenSize();

        if ((mouse.pos() >= screen_pos).all() && (mouse.pos() < screen_pos + screen_size).all() && button_pressed)
        {
            button_pressed = 0;
            Activate();
        }

        std::string saved_value = value;
        if (id == active_id)
            Input::Text(&value, max_chars, allowed_chars);

        if (tick_func)
            tick_func(*this, value != saved_value);
    }

    void Render() const
    {
        if (!visible)
            return;

        ivec2 screen_pos = ScreenPos(),
              screen_size = ScreenSize();

        // Box
        r.Quad(screen_pos, screen_size).color(fvec3(0));
        r.Quad(screen_pos+1, screen_size-2).color(fvec3(0.95));

        // Title
        r.Text(screen_pos, title).color(title_color).font(font_small).align(ivec2(-1,1));

        // Invalid text
        r.Text(screen_pos.add_y(height), invalid_text).color(title_color).font(font_small).align(ivec2(-1,-1)).color(invalid_color);

        { // Text
            auto text = r.Text(screen_pos + ivec2(text_offset_x, height/2), value).align_h(-1).font(font_small).color(fvec3(0));
            if (invalid)
                text.preset(Draw::ColorAfterPos(invalid_pos, invalid_color));
            if (id == active_id && tick_stabilizer.ticks % 60 < 30)
                text.preset(Draw::WithCursor(Input::TextCursorPos(), fvec3(0)));
        }

        if (render_func)
            render_func(*this);
    }
};


int main(int, char **)
{
    const std::string default_expression = "0";
    constexpr fvec3 plot_color = fvec3(0.9,0,0.66);

    Draw::Init();

    Graphics::ClearColor(fvec3(1));
    Graphics::Clear(Graphics::color);
    Draw::Accumulator::Overwrite();

    enum class State {main, real_imag};
    State cur_state = State::main;

    long double freq_min = Plot::default_min, freq_max = Plot::default_max;

    Expression e(default_expression);

    auto func_main = [&e](long double t){return e.EvalVec({0,t});};
    auto func_real = [&e](long double t){return ldvec2(t,e.EvalVec({0,t}).x);};
    auto func_imag = [&e](long double t){return ldvec2(t,e.EvalVec({0,t}).y);};

    Plot plot;

    static auto PlotFlags = [&]() -> int
    {
        switch (cur_state)
        {
          case State::main:
            return Plot::lock_scale_ratio;
          default:
            return 0;
        }
        return 0;
    };

    std::list<Button> buttons;
    std::list<TextField> text_fields;

    enum class InterfaceObj {func_input, range_input, scale_xy, scale, mode};

    bool need_interface_reset = 0;

    static auto AddInterface = [&](InterfaceObj category)
    {
        constexpr float scale_factor = 1.01;

        constexpr int range_input_w = 64, input_gap_w = 32;

        switch (category)
        {
          case InterfaceObj::func_input:
            text_fields.push_back(TextField(ivec2(-1,-1), ivec2(24+range_input_w*2 + input_gap_w*2, 80), 9000, 10, "Передаточная функция W(s)", "01234567890.,+-*/^()sj ", [&](TextField &ref, bool upd)
            {
                ref.width = win.Size().x - ref.pos.x - 24;
                ref.max_chars = ref.width / 8;
                if (upd)
                {
                    plot.ResetAccumulator();
                    try
                    {
                        e = Expression(ref.value);
                        ref.invalid = 0;
                        ref.invalid_pos = 0;
                        ref.invalid_text = "";
                    }
                    catch (Expression::Exception &exc)
                    {
                        e = {};
                        ref.invalid = 1;
                        ref.invalid_pos = exc.pos;
                        ref.invalid_text = exc.message;
                    }
                    need_interface_reset = 1;
                }
            }));
            text_fields.back().value = default_expression;
            break;
          case InterfaceObj::range_input:
            {
                auto Lambda = [&](TextField &ref, long double &param)
                {
                    if (auto it = ref.value.begin(); it != ref.value.end())
                        ref.value.erase(std::remove(++it, ref.value.end(), '-'), ref.value.end());
                    if (auto it = std::find(ref.value.begin(), ref.value.end(), '.'); it != ref.value.end())
                        ref.value.erase(std::remove(++it, ref.value.end(), '.'), ref.value.end());
                    std::string value_copy = ref.value;
                    std::replace(value_copy.begin(), value_copy.end(), ',', '.');
                    if (value_copy.empty())
                        value_copy = "0";
                    else if (value_copy == "-")
                        value_copy = "-0";
                    Reflection::from_string(param, value_copy.c_str());
                    need_interface_reset = 1;
                };
                text_fields.push_back(TextField(ivec2(-1,-1), ivec2(24, 80), 64, 6, "Мин. частота", "0123456789.-", [&, Lambda](TextField &ref, bool upd)
                {
                    if (upd)
                    {
                        Lambda(ref, freq_min);
                    }
                }));
                text_fields.back().value = Reflection::to_string(Plot::default_min);
                text_fields.push_back(TextField(ivec2(-1,-1), ivec2(24+range_input_w+input_gap_w, 80), 64, 6, "Макс. частота", "0123456789.-", [&, Lambda](TextField &ref, bool upd)
                {
                    if (upd)
                    {
                        Lambda(ref, freq_max);
                    }
                }));
                text_fields.back().value = Reflection::to_string(Plot::default_max);
            }
            break;
          case InterfaceObj::scale:
            {
                int x = -32;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 0, 0, "Сбросить расположение графика", [&]{plot.ResetOffsetAndScale();})); // Reset offset and scale
                x -= 48+8;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 2, 1, "Уменьшить масштаб", [&]{plot.Scale(ldvec2(1,1/scale_factor));})); // Decrease scale
                x -= 48;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 1, 1, "Увеличить масштаб", [&]{plot.Scale(ldvec2(1,scale_factor));})); // Increase scale
            }
            break;
          case InterfaceObj::scale_xy:
            {
                int x = -32;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 0, 0, "Сбросить расположение графика", [&]{plot.ResetOffsetAndScale();})); // Reset offset and scale
                x -= 48+8;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 6, 1, "Уменьшить масштаб по оси Y", [&]{plot.Scale(ldvec2(1,1/scale_factor));})); // Decrease Y scale
                x -= 48;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 5, 1, "Увеличить масштаб по оси Y", [&]{plot.Scale(ldvec2(1,scale_factor));})); // Increase Y scale
                x -= 48+8;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 4, 1, "Уменьшить масштаб по оси X", [&]{plot.Scale(ldvec2(1/scale_factor,1));})); // Decrease X scale
                x -= 48;
                buttons.push_back(Button(ivec2(1,-1), ivec2(x,32), 3, 1, "Увеличить масштаб по оси X", [&]{plot.Scale(ldvec2(scale_factor,1));})); // Increase X scale
            }
            break;
          case InterfaceObj::mode:
            {
                int x = 32;
                // Primary mode
                buttons.push_back(Button(ivec2(-1,-1), ivec2(x,32), 7, 0, "Годограф", [&]{
                    cur_state = State::main;
                    need_interface_reset = 1;
                }));
                x += 48+8;
                // Real&imaginary mode
                buttons.push_back(Button(ivec2(-1,-1), ivec2(x,32), 8, 0, "Действительная и мнимая части", [&]{
                    cur_state = State::real_imag;
                    need_interface_reset = 1;
                }));
                x += 48+8;
            }
            break;
        }
    };

    auto ResetInterface = [&]()
    {
       if (!bool(e))
            plot = Plot(PlotFlags());
        else
        {
            switch (cur_state)
            {
              case State::main:
                plot = Plot({{func_main, plot_color}}, freq_min, freq_max, PlotFlags());
                break;
              case State::real_imag:
                plot = Plot({{func_real, fvec3(1,0,0)},{func_imag, fvec3(0,1,0)}}, freq_min, freq_max, PlotFlags());
                break;
            }
        }

        buttons = {};

        AddInterface(cur_state == State::main ? InterfaceObj::scale : InterfaceObj::scale_xy);
        AddInterface(InterfaceObj::mode);
    };
    AddInterface(InterfaceObj::func_input);
    AddInterface(InterfaceObj::range_input);
    ResetInterface();

    auto Tick = [&]
    {
        Draw::Accumulator::Return();

        bool button_pressed = mouse.left.pressed();

        if (button_pressed)
            TextField::Deactivate();
        for (auto &text_field : text_fields)
            text_field.Tick(button_pressed);

        Button::ResetTooltip();
        for (auto &button : buttons)
            button.Tick(button_pressed);

        if (need_interface_reset)
        {
            need_interface_reset = 0;
            ResetInterface();
        }

        if (button_pressed)
            plot.Grab();

        plot.Tick(127); // This should be `2^n - 1` for plot to look pretty while moving.

        Draw::Accumulator::Overwrite();
    };
    auto Render = [&]
    {
        constexpr float interface_rect_alpha = 0.95,
                        interface_rect_alpha2 = 0.75;

        Draw::Accumulator::Return();

        // Interface background
        r.Quad(Draw::min, ivec2(win.Size().x, interface_rect_height)).color(fvec3(1)).alpha(interface_rect_alpha, interface_rect_alpha, interface_rect_alpha2, interface_rect_alpha2);
        r.Quad(Draw::min.add_y(interface_rect_height), ivec2(win.Size().x, 1)).color(fvec3(0));

        // Text fields
        for (auto &text_field : text_fields)
            text_field.Render();

        // Buttons
        for (const auto &button : buttons)
            button.Render();

        { // Button tooltips
            constexpr int tooltip_box_height = 32,
                          tooltip_box_gap = 32;
            constexpr fvec3 tooltip_box_line_color = fvec3(0.3),
                            tooltip_box_fill_color = fvec3(0.95),
                            tooltip_box_text_color = fvec3(0);

            if (Button::CurrentTooltip().size() > 0)
            {
                r.Quad(ivec2(Draw::min.x, Draw::max.y - tooltip_box_height - tooltip_box_gap), ivec2(win.Size().x, tooltip_box_height)).color(tooltip_box_fill_color);
                r.Quad(ivec2(Draw::min.x, Draw::max.y - tooltip_box_height - tooltip_box_gap), ivec2(win.Size().x, 1)).color(tooltip_box_line_color);
                r.Quad(ivec2(Draw::min.x, Draw::max.y - tooltip_box_height), ivec2(win.Size().x, 1)).color(tooltip_box_line_color);
                r.Text(ivec2(0, Draw::max.y - tooltip_box_height/2 - tooltip_box_gap), Button::CurrentTooltip()).color(tooltip_box_text_color);
            }
        }
    };

    uint64_t frame_start = Timing::Clock();

    while (1)
    {
        uint64_t time = Timing::Clock(), frame_delta = time - frame_start;
        frame_start = time;

        while (tick_stabilizer.Tick(frame_delta))
        {
            Events::Process();
            if (win.size_changed)
            {
                win.size_changed = 0;
                Graphics::Viewport(win.Size());
                Draw::HandleResize();
                plot.RecalculateDefaultOffsetAndScale();
                plot.ResetAccumulator();
            }
            Tick();
        }

        Graphics::CheckErrors();

        Render();
        r.Finish();

        win.Swap();
    }
}
