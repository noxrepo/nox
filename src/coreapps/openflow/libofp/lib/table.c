/*
 * Copyright (c) 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "table.h"

#include "dynamic-string.h"
#include "json.h"
#include "ovsdb-data.h"
#include "ovsdb-error.h"
#include "timeval.h"
#include "util.h"

struct column {
    char *heading;
};

static char *
cell_to_text(struct cell *cell, const struct table_style *style)
{
    if (!cell->text) {
        if (cell->json) {
            if (style->cell_format == CF_JSON || !cell->type) {
                cell->text = json_to_string(cell->json, JSSF_SORT);
            } else {
                struct ovsdb_datum datum;
                struct ovsdb_error *error;
                struct ds s;

                error = ovsdb_datum_from_json(&datum, cell->type, cell->json,
                                              NULL);
                if (!error) {
                    ds_init(&s);
                    if (style->cell_format == CF_STRING) {
                        ovsdb_datum_to_string(&datum, cell->type, &s);
                    } else {
                        ovsdb_datum_to_bare(&datum, cell->type, &s);
                    }
                    ovsdb_datum_destroy(&datum, cell->type);
                    cell->text = ds_steal_cstr(&s);
                } else {
                    cell->text = json_to_string(cell->json, JSSF_SORT);
                    ovsdb_error_destroy(error);
                }
            }
        } else {
            cell->text = xstrdup("");
        }
    }

    return cell->text;
}

static void
cell_destroy(struct cell *cell)
{
    free(cell->text);
    json_destroy(cell->json);
}

/* Initializes 'table' as an empty table.
 *
 * The caller should then:
 *
 * 1. Call table_add_column() once for each column.
 * 2. For each row:
 *    2a. Call table_add_row().
 *    2b. For each column in the cell, call table_add_cell() and fill in
 *        the returned cell.
 * 3. Call table_print() to print the final table.
 * 4. Free the table with table_destroy().
 */
void
table_init(struct table *table)
{
    memset(table, 0, sizeof *table);
}

/* Destroys 'table' and frees all associated storage.  (However, the client
 * owns the 'type' members pointed to by cells, so these are not destroyed.) */
void
table_destroy(struct table *table)
{
    if (table) {
        size_t i;

        for (i = 0; i < table->n_columns; i++) {
            free(table->columns[i].heading);
        }
        free(table->columns);

        for (i = 0; i < table->n_columns * table->n_rows; i++) {
            cell_destroy(&table->cells[i]);
        }
        free(table->cells);

        free(table->caption);
    }
}

/* Sets 'caption' as the caption for 'table'.
 *
 * 'table' takes ownership of 'caption'. */
void
table_set_caption(struct table *table, char *caption)
{
    free(table->caption);
    table->caption = caption;
}

/* Turns printing a timestamp along with 'table' on or off, according to
 * 'timestamp'.  */
void
table_set_timestamp(struct table *table, bool timestamp)
{
    table->timestamp = timestamp;
}

/* Adds a new column to 'table' just to the right of any existing column, with
 * 'heading' as a title for the column.  'heading' must be a valid printf()
 * format specifier.
 *
 * Columns must be added before any data is put into 'table'. */
void
table_add_column(struct table *table, const char *heading, ...)
{
    struct column *column;
    va_list args;

    ovs_assert(!table->n_rows);
    if (table->n_columns >= table->allocated_columns) {
        table->columns = x2nrealloc(table->columns, &table->allocated_columns,
                                    sizeof *table->columns);
    }
    column = &table->columns[table->n_columns++];

    va_start(args, heading);
    column->heading = xvasprintf(heading, args);
    va_end(args);
}

static struct cell *
table_cell__(const struct table *table, size_t row, size_t column)
{
    return &table->cells[column + row * table->n_columns];
}

/* Adds a new row to 'table'.  The table's columns must already have been added
 * with table_add_column().
 *
 * The row is initially empty; use table_add_cell() to start filling it in. */
void
table_add_row(struct table *table)
{
    size_t x, y;

    if (table->n_rows >= table->allocated_rows) {
        table->cells = x2nrealloc(table->cells, &table->allocated_rows,
                                  table->n_columns * sizeof *table->cells);
    }

    y = table->n_rows++;
    table->current_column = 0;
    for (x = 0; x < table->n_columns; x++) {
        struct cell *cell = table_cell__(table, y, x);
        memset(cell, 0, sizeof *cell);
    }
}

/* Adds a new cell in the current row of 'table', which must have been added
 * with table_add_row().  Cells are filled in the same order that the columns
 * were added with table_add_column().
 *
 * The caller is responsible for filling in the returned cell, in one of two
 * fashions:
 *
 *   - If the cell should contain an ovsdb_datum, formatted according to the
 *     table style, then fill in the 'json' member with the JSON representation
 *     of the datum and 'type' with its type.
 *
 *   - If the cell should contain a fixed text string, then the caller should
 *     assign that string to the 'text' member.  This is undesirable if the
 *     cell actually contains OVSDB data because 'text' cannot be formatted
 *     according to the table style; it is always output verbatim.
 */
struct cell *
table_add_cell(struct table *table)
{
    size_t x, y;

    ovs_assert(table->n_rows > 0);
    ovs_assert(table->current_column < table->n_columns);

    x = table->current_column++;
    y = table->n_rows - 1;

    return table_cell__(table, y, x);
}

static void
table_print_table_line__(struct ds *line)
{
    puts(ds_cstr(line));
    ds_clear(line);
}

static char *
table_format_timestamp__(void)
{
    return xastrftime_msec("%Y-%m-%d %H:%M:%S.###", time_wall_msec(), true);
}

static void
table_print_timestamp__(const struct table *table)
{
    if (table->timestamp) {
        char *s = table_format_timestamp__();
        puts(s);
        free(s);
    }
}

static void
table_print_table__(const struct table *table, const struct table_style *style)
{
    static int n = 0;
    struct ds line = DS_EMPTY_INITIALIZER;
    int *widths;
    size_t x, y;

    if (n++ > 0) {
        putchar('\n');
    }

    table_print_timestamp__(table);

    if (table->caption) {
        puts(table->caption);
    }

    widths = xmalloc(table->n_columns * sizeof *widths);
    for (x = 0; x < table->n_columns; x++) {
        const struct column *column = &table->columns[x];

        widths[x] = strlen(column->heading);
        for (y = 0; y < table->n_rows; y++) {
            const char *text = cell_to_text(table_cell__(table, y, x), style);
            size_t length = strlen(text);

            if (length > widths[x]) {
                widths[x] = length;
            }
        }
    }

    if (style->headings) {
        for (x = 0; x < table->n_columns; x++) {
            const struct column *column = &table->columns[x];
            if (x) {
                ds_put_char(&line, ' ');
            }
            ds_put_format(&line, "%-*s", widths[x], column->heading);
        }
        table_print_table_line__(&line);

        for (x = 0; x < table->n_columns; x++) {
            if (x) {
                ds_put_char(&line, ' ');
            }
            ds_put_char_multiple(&line, '-', widths[x]);
        }
        table_print_table_line__(&line);
    }

    for (y = 0; y < table->n_rows; y++) {
        for (x = 0; x < table->n_columns; x++) {
            const char *text = cell_to_text(table_cell__(table, y, x), style);
            if (x) {
                ds_put_char(&line, ' ');
            }
            ds_put_format(&line, "%-*s", widths[x], text);
        }
        table_print_table_line__(&line);
    }

    ds_destroy(&line);
    free(widths);
}

static void
table_print_list__(const struct table *table, const struct table_style *style)
{
    static int n = 0;
    size_t x, y;

    if (n++ > 0) {
        putchar('\n');
    }

    table_print_timestamp__(table);

    if (table->caption) {
        puts(table->caption);
    }

    for (y = 0; y < table->n_rows; y++) {
        if (y > 0) {
            putchar('\n');
        }
        for (x = 0; x < table->n_columns; x++) {
            const char *text = cell_to_text(table_cell__(table, y, x), style);
            if (style->headings) {
                printf("%-20s: ", table->columns[x].heading);
            }
            puts(text);
        }
    }
}

static void
table_escape_html_text__(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        char c = s[i];

        switch (c) {
        case '&':
            fputs("&amp;", stdout);
            break;
        case '<':
            fputs("&lt;", stdout);
            break;
        case '>':
            fputs("&gt;", stdout);
            break;
        case '"':
            fputs("&quot;", stdout);
            break;
        default:
            putchar(c);
            break;
        }
    }
}

static void
table_print_html_cell__(const char *element, const char *content)
{
    const char *p;

    printf("    <%s>", element);
    for (p = content; *p; ) {
        struct uuid uuid;

        if (uuid_from_string_prefix(&uuid, p)) {
            printf("<a href=\"#%.*s\">%.*s</a>", UUID_LEN, p, 8, p);
            p += UUID_LEN;
        } else {
            table_escape_html_text__(p, 1);
            p++;
        }
    }
    printf("</%s>\n", element);
}

static void
table_print_html__(const struct table *table, const struct table_style *style)
{
    size_t x, y;

    table_print_timestamp__(table);

    fputs("<table border=1>\n", stdout);

    if (table->caption) {
        table_print_html_cell__("caption", table->caption);
    }

    if (style->headings) {
        fputs("  <tr>\n", stdout);
        for (x = 0; x < table->n_columns; x++) {
            const struct column *column = &table->columns[x];
            table_print_html_cell__("th", column->heading);
        }
        fputs("  </tr>\n", stdout);
    }

    for (y = 0; y < table->n_rows; y++) {
        fputs("  <tr>\n", stdout);
        for (x = 0; x < table->n_columns; x++) {
            const char *content;

            content = cell_to_text(table_cell__(table, y, x), style);
            if (!strcmp(table->columns[x].heading, "_uuid")) {
                fputs("    <td><a name=\"", stdout);
                table_escape_html_text__(content, strlen(content));
                fputs("\">", stdout);
                table_escape_html_text__(content, 8);
                fputs("</a></td>\n", stdout);
            } else {
                table_print_html_cell__("td", content);
            }
        }
        fputs("  </tr>\n", stdout);
    }

    fputs("</table>\n", stdout);
}

static void
table_print_csv_cell__(const char *content)
{
    const char *p;

    if (!strpbrk(content, "\n\",")) {
        fputs(content, stdout);
    } else {
        putchar('"');
        for (p = content; *p != '\0'; p++) {
            switch (*p) {
            case '"':
                fputs("\"\"", stdout);
                break;
            default:
                putchar(*p);
                break;
            }
        }
        putchar('"');
    }
}

static void
table_print_csv__(const struct table *table, const struct table_style *style)
{
    static int n = 0;
    size_t x, y;

    if (n++ > 0) {
        putchar('\n');
    }

    table_print_timestamp__(table);

    if (table->caption) {
        puts(table->caption);
    }

    if (style->headings) {
        for (x = 0; x < table->n_columns; x++) {
            const struct column *column = &table->columns[x];
            if (x) {
                putchar(',');
            }
            table_print_csv_cell__(column->heading);
        }
        putchar('\n');
    }

    for (y = 0; y < table->n_rows; y++) {
        for (x = 0; x < table->n_columns; x++) {
            if (x) {
                putchar(',');
            }
            table_print_csv_cell__(cell_to_text(table_cell__(table, y, x),
                                                style));
        }
        putchar('\n');
    }
}

static void
table_print_json__(const struct table *table, const struct table_style *style)
{
    struct json *json, *headings, *data;
    size_t x, y;
    char *s;

    json = json_object_create();
    if (table->caption) {
        json_object_put_string(json, "caption", table->caption);
    }
    if (table->timestamp) {
        char *s = table_format_timestamp__();
        json_object_put_string(json, "time", s);
        free(s);
    }

    headings = json_array_create_empty();
    for (x = 0; x < table->n_columns; x++) {
        const struct column *column = &table->columns[x];
        json_array_add(headings, json_string_create(column->heading));
    }
    json_object_put(json, "headings", headings);

    data = json_array_create_empty();
    for (y = 0; y < table->n_rows; y++) {
        struct json *row = json_array_create_empty();
        for (x = 0; x < table->n_columns; x++) {
            const struct cell *cell = table_cell__(table, y, x);
            if (cell->text) {
                json_array_add(row, json_string_create(cell->text));
            } else if (cell->json) {
                json_array_add(row, json_clone(cell->json));
            } else {
                json_array_add(row, json_null_create());
            }
        }
        json_array_add(data, row);
    }
    json_object_put(json, "data", data);

    s = json_to_string(json, style->json_flags);
    json_destroy(json);
    puts(s);
    free(s);
}

/* Parses 'format' as the argument to a --format command line option, updating
 * 'style->format'. */
void
table_parse_format(struct table_style *style, const char *format)
{
    if (!strcmp(format, "table")) {
        style->format = TF_TABLE;
    } else if (!strcmp(format, "list")) {
        style->format = TF_LIST;
    } else if (!strcmp(format, "html")) {
        style->format = TF_HTML;
    } else if (!strcmp(format, "csv")) {
        style->format = TF_CSV;
    } else if (!strcmp(format, "json")) {
        style->format = TF_JSON;
    } else {
        ovs_fatal(0, "unknown output format \"%s\"", format);
    }
}

/* Parses 'format' as the argument to a --data command line option, updating
 * 'style->cell_format'. */
void
table_parse_cell_format(struct table_style *style, const char *format)
{
    if (!strcmp(format, "string")) {
        style->cell_format = CF_STRING;
    } else if (!strcmp(format, "bare")) {
        style->cell_format = CF_BARE;
    } else if (!strcmp(format, "json")) {
        style->cell_format = CF_JSON;
    } else {
        ovs_fatal(0, "unknown data format \"%s\"", format);
    }
}

/* Outputs 'table' on stdout in the specified 'style'. */
void
table_print(const struct table *table, const struct table_style *style)
{
    switch (style->format) {
    case TF_TABLE:
        table_print_table__(table, style);
        break;

    case TF_LIST:
        table_print_list__(table, style);
        break;

    case TF_HTML:
        table_print_html__(table, style);
        break;

    case TF_CSV:
        table_print_csv__(table, style);
        break;

    case TF_JSON:
        table_print_json__(table, style);
        break;
    }
}
