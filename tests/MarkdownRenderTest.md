# Markdown Render Test

This document is a broad Markdown fixture for checking note rendering with md4c.
It intentionally mixes CommonMark elements with common extensions such as tables,
strikethrough, task lists, and autolinks.

## Headings

# Heading 1

## Heading 2

### Heading 3

#### Heading 4

##### Heading 5

###### Heading 6

Alternate heading level 1
=========================

Alternate heading level 2
-------------------------

## Paragraphs And Line Breaks

This is a normal paragraph with multiple sentences. It should wrap naturally in
the note viewer without adding extra spacing inside the paragraph.

This is another paragraph separated by a blank line.

This line ends with two spaces.  
The next line should appear after a hard line break.

This line uses a backslash for a hard break.\
The next line should also appear after a hard line break.

## Inline Formatting

Plain text, *emphasis*, _emphasis with underscores_, **strong emphasis**,
__strong emphasis with underscores__, ***strong and emphasized***, and
~~strikethrough~~.

Inline code should keep spacing: `std::vector<int> values = {1, 2, 3};`

Escaped punctuation should render as punctuation instead of syntax:
\*not emphasized\*, \[not a link\], \`not code\`, and \# not a heading.

Superscript-like text is not standard Markdown: 2^10^.
Subscript-like text is not standard Markdown: H~2~O.

## Links

An inline link to [CommonMark](https://commonmark.org/).

An inline link with a title: [MD4C](https://github.com/mity/md4c "MD4C GitHub").

A reference link to [the Markdown guide][markdown-guide].

A shortcut reference link to [CommonMark].

Bare URLs and emails may render as links when the extension is enabled:
https://example.com/path?query=1#section
user@example.com

Angle-bracket autolinks:
<https://example.org/autolink>
<user@example.org>

[markdown-guide]: https://www.markdownguide.org/
[CommonMark]: https://commonmark.org/help/

## Images

Inline image:

![Small placeholder image](https://via.placeholder.com/120x80.png?text=MD)

Reference image:

![Reference placeholder][placeholder-image]

[placeholder-image]: https://via.placeholder.com/160x90.png?text=Markdown

## Blockquotes

> A simple blockquote.
>
> It can contain multiple paragraphs.

> Nested blockquotes:
>
> > Second level quote.
> >
> > > Third level quote.

> Blockquote with other Markdown:
>
> - Quoted list item
> - Another quoted list item with **strong text**
>
> ```text
> quoted fenced code
> ```

## Unordered Lists

- First item
- Second item
- Third item

* Asterisk item
* Another asterisk item

+ Plus item
+ Another plus item

Nested unordered list:

- Parent item
  - Child item
    - Grandchild item
  - Another child item
- Second parent item

List items with paragraphs:

- First item paragraph.

  Continuation paragraph inside the first item.

- Second item paragraph.

  > Blockquote inside a list item.

  ```cpp
  int value = 42;
  ```

## Ordered Lists

1. First item
2. Second item
3. Third item

Ordered list starting at a different number:

4. Starts at four
5. Continues at five
6. Continues at six

Nested ordered list:

1. Parent one
   1. Child one
   2. Child two
2. Parent two
   1. Child one
      1. Grandchild one

Mixed list:

1. Install dependencies
   - CMake
   - A C++ compiler
   - md4c
2. Build the project
3. Open the note viewer

## Task Lists

- [x] Completed task
- [ ] Incomplete task
- [X] Completed task with uppercase X
- [ ] Task item with **formatting**, `inline code`, and a [link](https://example.com/)

## Code Blocks

Indented code block:

    #include <iostream>

    int main() {
        std::cout << "Indented code block\n";
        return 0;
    }

Fenced code block without a language:

```
plain fenced code
line 2
line 3
```

Fenced code block with a language:

```cpp
#include <string>
#include <vector>

struct Note {
    std::string title;
    std::string markdown;
};
```

Fenced code block with backticks inside:

````markdown
```cpp
int nested = 1;
```
````

Fenced code block using tildes:

~~~json
{
  "name": "markdown-render-test",
  "enabled": true,
  "items": [1, 2, 3]
}
~~~

## Tables

| Feature | Syntax | Expected Result |
| --- | --- | --- |
| Emphasis | `*text*` | *Italic text* |
| Strong | `**text**` | **Bold text** |
| Code | `` `code` `` | `Monospace text` |
| Link | `[text](url)` | [Clickable link](https://example.com/) |

Alignment:

| Left aligned | Center aligned | Right aligned |
| :--- | :---: | ---: |
| alpha | beta | 123 |
| longer text | centered | 4567 |
| short | value | 89 |

Table with inline formatting:

| Item | Notes |
| --- | --- |
| `code` | A cell containing inline code |
| **bold** | A cell containing strong text |
| [link](https://example.com/) | A cell containing a link |

## Thematic Breaks

Three hyphens:

---

Three asterisks:

***

Three underscores:

___

## Raw HTML

Inline HTML: This word is <mark>highlighted</mark>, and this one is
<span style="color: red;">red when inline styles are allowed</span>.

Block HTML:

<details>
<summary>Expandable details</summary>

This content is inside a raw HTML block.

</details>

<table>
  <tr>
    <th>HTML table header</th>
    <th>Value</th>
  </tr>
  <tr>
    <td>HTML table cell</td>
    <td>42</td>
  </tr>
</table>

## Backslash Escapes

\! \# \$ \% \& \' \( \) \* \+ \, \- \. \/ \: \; \< \= \> \? \@ \[ \\ \] \^ \_ \` \{ \| \} \~

## Entity References

Named entities: &amp; &lt; &gt; &quot; &copy; &mdash;

Decimal entity: &#169;

Hex entity: &#x1F600;

## Common Edge Cases

Paragraph immediately followed by a list:
- This should be a list item.
- This should be another list item.

Paragraph with underscores inside words: foo_bar_baz should not emphasize the
middle word.

Emphasis around punctuation: **bold**, *italic*, and `code`.

A link containing parentheses:
[function call](https://example.com/docs/function(arg))

A URL containing escaped parentheses:
[escaped parentheses](https://example.com/docs/function\(arg\))

Inline code containing Markdown markers:
`**not bold**`, `# not heading`, and `[not a link](https://example.com/)`.

## Optional Extension Samples

These items are useful when md4c extensions are enabled. If they render as plain
text, the parser probably does not enable that extension.

Math span: $E = mc^2$, $x_i^2 + \alpha \le \sqrt{n}$, and $\frac{a+b}{c+d}$.

Display math:

$$
\int_0^1 x^2 dx = \frac{1}{3}
$$

$$
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
$$

Wiki link: [[Markdown Render Test]]

Underline extension: __underlined if underline extension is enabled__

## Final Paragraph

The final paragraph verifies that the renderer closes all open blocks correctly
after lists, blockquotes, tables, code fences, HTML blocks, and thematic breaks.
