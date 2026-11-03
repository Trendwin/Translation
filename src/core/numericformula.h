#ifndef NUMERICFORMULA_H
#define NUMERICFORMULA_H

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <limits>

// A small arithmetic language, deliberately independent of JavaScript or a device protocol.
// Reduced fractions keep decimal literals and divisions exact. An intermediate that cannot
// be represented by signed 64-bit numerator/positive denominator fails before arithmetic.
namespace NumericFormula {
namespace Detail {
inline qint64 maximum() { return (std::numeric_limits<qint64>::max)(); }
inline qint64 magnitude(qint64 value) { return value < 0 ? -value : value; }
inline qint64 gcd(qint64 a, qint64 b)
{
    while (b) { const qint64 remainder = a % b; a = b; b = remainder; }
    return a;
}
inline bool multiply(qint64 a, qint64 b, qint64 *out)
{
    if (a && magnitude(b) > maximum() / magnitude(a)) return false;
    *out = a * b; return true;
}
inline bool add(qint64 a, qint64 b, qint64 *out)
{
    if ((b > 0 && a > maximum() - b) || (b < 0 && a < -maximum() - b)) return false;
    *out = a + b; return true;
}
struct Fraction {
    qint64 numerator = 0, denominator = 1;
    Fraction() = default;
    explicit Fraction(qint64 value) : numerator(value) {}
    void reduce()
    {
        const qint64 common = gcd(magnitude(numerator), denominator);
        numerator /= common; denominator /= common;
    }
};

class Parser {
public:
    Parser(const QString &text, const QMap<QString, qint64> *values)
        : m_text(text), m_values(values) {}

    bool parse(Fraction *out)
    {
        if (m_text.isEmpty() || m_text.size() > 1024)
            return fail(QString::fromUtf8(u8"公式需填写1～1024个字符"));
        if (!sum(out, 0)) return false;
        spaces();
        if (m_position != m_text.size()) return fail(QString::fromUtf8(u8"无法识别此处的符号"));
        return true;
    }
    QString error;
    QSet<QString> used;

private:
    const QString &m_text;
    const QMap<QString, qint64> *m_values;
    int m_position = 0, m_nodes = 0;
    bool fail(const QString &reason)
    {
        if (error.isEmpty()) error = QString::fromUtf8(u8"公式第%1个字符：%2").arg(m_position + 1).arg(reason);
        return false;
    }
    void spaces() { while (m_position < m_text.size() && m_text.at(m_position).isSpace()) ++m_position; }
    static bool letter(QChar ch)
    {
        return (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) || ch == QLatin1Char('_');
    }
    static bool digit(QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); }
    bool node()
    {
        if (++m_nodes > 256) return fail(QString::fromUtf8(u8"公式超过256个运算或数值上限"));
        return true;
    }
    bool combine(Fraction *left, Fraction right, QChar operation)
    {
        if (!node()) return false;
        if (!m_values) { *left = Fraction(1); return true; }
        if (operation == QLatin1Char('+') || operation == QLatin1Char('-')) {
            if (operation == QLatin1Char('-')) right.numerator = -right.numerator;
            const qint64 common = gcd(left->denominator, right.denominator);
            qint64 a, b, result, denominator;
            if (!multiply(left->numerator, right.denominator / common, &a)
                    || !multiply(right.numerator, left->denominator / common, &b)
                    || !add(a, b, &result)
                    || !multiply(left->denominator, right.denominator / common, &denominator))
                return fail(QString::fromUtf8(u8"中间计算超出64位精确算术范围"));
            left->numerator = result; left->denominator = denominator;
        } else {
            if (operation == QLatin1Char('/')) {
                if (!right.numerator) return fail(QString::fromUtf8(u8"除数不能为0"));
                const qint64 sign = right.numerator < 0 ? -1 : 1;
                const qint64 previousNumerator = right.numerator;
                right.numerator = sign * right.denominator;
                right.denominator = magnitude(previousNumerator);
            }
            const qint64 commonA = gcd(magnitude(left->numerator), right.denominator);
            const qint64 commonB = gcd(magnitude(right.numerator), left->denominator);
            qint64 numerator, denominator;
            if (!multiply(left->numerator / commonA, right.numerator / commonB, &numerator)
                    || !multiply(left->denominator / commonB, right.denominator / commonA, &denominator))
                return fail(QString::fromUtf8(u8"中间计算超出64位精确算术范围"));
            left->numerator = numerator; left->denominator = denominator;
        }
        left->reduce(); return true;
    }
    bool sum(Fraction *out, int depth)
    {
        if (!product(out, depth)) return false;
        for (;;) {
            spaces();
            if (m_position == m_text.size() || (m_text.at(m_position) != QLatin1Char('+') && m_text.at(m_position) != QLatin1Char('-'))) return true;
            const QChar operation = m_text.at(m_position++); Fraction right;
            if (!product(&right, depth) || !combine(out, right, operation)) return false;
        }
    }
    bool product(Fraction *out, int depth)
    {
        if (!atom(out, depth)) return false;
        for (;;) {
            spaces();
            if (m_position == m_text.size() || (m_text.at(m_position) != QLatin1Char('*') && m_text.at(m_position) != QLatin1Char('/'))) return true;
            const QChar operation = m_text.at(m_position++); Fraction right;
            if (!atom(&right, depth) || !combine(out, right, operation)) return false;
        }
    }
    bool atom(Fraction *out, int depth)
    {
        if (depth > 32) return fail(QString::fromUtf8(u8"括号或正负号嵌套超过32层"));
        if (!node()) return false;
        spaces();
        if (m_position == m_text.size()) return fail(QString::fromUtf8(u8"缺少数值或变量"));
        QChar ch = m_text.at(m_position);
        if (ch == QLatin1Char('+') || ch == QLatin1Char('-')) {
            ++m_position;
            if (!atom(out, depth + 1)) return false;
            if (ch == QLatin1Char('-')) out->numerator = -out->numerator;
            return true;
        }
        if (ch == QLatin1Char('(')) {
            ++m_position;
            if (!sum(out, depth + 1)) return false;
            spaces();
            if (m_position == m_text.size() || m_text.at(m_position) != QLatin1Char(')'))
                return fail(QString::fromUtf8(u8"缺少右括号"));
            ++m_position; return true;
        }
        if (letter(ch)) {
            const int start = m_position++;
            while (m_position < m_text.size() && (letter(m_text.at(m_position)) || digit(m_text.at(m_position)))) ++m_position;
            const QString name = m_text.mid(start, m_position - start); used.insert(name);
            if (used.size() > 64) return fail(QString::fromUtf8(u8"公式最多使用64个变量"));
            if (m_values && !m_values->contains(name)) return fail(QString::fromUtf8(u8"变量“%1”没有绑定来源").arg(name));
            const qint64 value = m_values ? m_values->value(name) : 1;
            if (value < -9007199254740991LL || value > 9007199254740991LL)
                return fail(QString::fromUtf8(u8"变量“%1”超出可精确保存的整数范围").arg(name));
            *out = Fraction(value); return true;
        }
        if (!digit(ch) && ch != QLatin1Char('.')) return fail(QString::fromUtf8(u8"这里只能填写数值、变量或括号"));
        qint64 numerator = 0, denominator = 1; bool decimal = false, anyDigit = false;
        while (m_position < m_text.size()) {
            ch = m_text.at(m_position);
            if (ch == QLatin1Char('.') && !decimal) { decimal = true; ++m_position; continue; }
            if (!digit(ch)) break;
            anyDigit = true; const int value = ch.unicode() - '0';
            if (numerator > (maximum() - value) / 10 || (decimal && denominator > maximum() / 10))
                return fail(QString::fromUtf8(u8"数字常量超出64位精确算术范围"));
            numerator = numerator * 10 + value;
            if (decimal) denominator *= 10;
            ++m_position;
        }
        if (!anyDigit) return fail(QString::fromUtf8(u8"小数点旁缺少数字"));
        out->numerator = numerator; out->denominator = denominator; out->reduce(); return true;
    }
};
}

inline bool inspect(const QString &text, QSet<QString> *used, QString *error)
{
    Detail::Parser parser(text, nullptr); Detail::Fraction ignored;
    const bool ok = parser.parse(&ignored);
    if (used) *used = parser.used;
    if (error) *error = parser.error;
    return ok;
}

inline bool evaluate(const QString &text, const QMap<QString, qint64> &values,
                     const QString &round, qint64 *output, QString *error)
{
    if (output) *output = 0;
    if (error) error->clear();
    const QString mode = round.isEmpty() ? QStringLiteral("nearest") : round;
    if (!QStringList({"exact", "floor", "ceil", "nearest"}).contains(mode)) {
        if (error) *error = QString::fromUtf8(u8"公式取整方式无效");
        return false;
    }
    Detail::Parser parser(text, &values); Detail::Fraction result;
    if (!parser.parse(&result)) { if (error) *error = parser.error; return false; }
    const qint64 remainder = result.numerator % result.denominator;
    if (mode == "exact" && remainder) {
        if (error) *error = QString::fromUtf8(u8"公式结果包含小数，请调整公式或选择取整方式");
        return false;
    }
    qint64 integer = result.numerator / result.denominator;
    if ((mode == "floor" && remainder < 0) || (mode == "nearest" && remainder < 0
            && Detail::magnitude(remainder) >= result.denominator - Detail::magnitude(remainder))) --integer;
    if ((mode == "ceil" && remainder > 0) || (mode == "nearest" && remainder > 0
            && remainder >= result.denominator - remainder)) ++integer;
    if (integer < -9007199254740991LL || integer > 9007199254740991LL) {
        if (error) *error = QString::fromUtf8(u8"公式结果超出可精确保存的整数范围");
        return false;
    }
    if (output) *output = integer;
    return true;
}
}
#endif
