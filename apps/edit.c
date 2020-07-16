/////////////////////////////////////////////////////////////////////////////
// Prototype of text editor
// Bobbi July 2020
/////////////////////////////////////////////////////////////////////////////

// TODO: We need to erase rest of screen to deal with updates where we are
//       close to end of buffer. Junk was being left behind at end of screen.
// TODO: Sometimes backspacing doesn't delete chars on screen.
// TODO: update_after_delete() does do CLREOL when it should sometimes (?)
// TODO: The code doesn't check for error cases when calling gap buffer
//       functions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <conio.h>
#include <peekpoke.h>

#define NCOLS     80 // Width of editing screen
#define NROWS     23 // Height of editing screen
#define CURSORROW 10 // Row cursor is initially shown on (if enough text)

#define EOL '\r' // For ProDOS

#define BELL    0x07
#define BACKSPC 0x08
#define NORMAL  0x0e
#define INVERSE 0x0f
#define CLREOL  0x1d

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;

#define BUFSZ 20000 // 65535
char     gapbuf[BUFSZ];
uint16_t gapbegin = 0;
uint16_t gapend = BUFSZ - 1;

uint8_t rowlen[NROWS]; // Number of chars on each row of screen

// Interface to read_char_update_pos()
uint8_t  do_print;
uint16_t pos;
uint8_t  row, col;

uint8_t cursrow, curscol; // Cursor position is kept here by draw_screen()

/*
 * Return number of bytes of freespace in gapbuf
 */
#define FREESPACE() (gapend - gapbegin + 1)

/*
 * Return number of bytes of data in gapbuf
 */
#define DATASIZE() (gapbegin + (BUFSZ - 1 - gapend))
/*
 * Obtain current position in gapbuf
 * This is the positon where the next character will be inserted
 */
#define GETPOS()    (gapbegin)

/*
 * Insert a character into gapbuf at current position
 * c - character to insert
 * Returns 0 on success, 1 on failure (insufficient space)
 */
uint8_t insert_char(char c) {
  if (FREESPACE()) {
    gapbuf[gapbegin++] = c;
    return 0;
  }
  return 1;
}

/*
 * Delete the character to the left of the current position
 * Returns 0 on success, 1 on failure (nothing to delete)
 */
uint8_t delete_char(void) {
  if (gapbegin == 0)
    return 1;
  --gapbegin;
}

/*
 * Delete the character to the right of the current position
 * Returns 0 on success, 1 on failure (nothing to delete)
 */
uint8_t delete_char_right(void) {
  if (gapend == BUFSZ - 1)
    return 1;
  ++gapend;
}

/*
 * Obtain the next character (to the right of the current position)
 * and advance the position.
 * c - character is returned throught this pointer
 * Returns 0 on success, 1 if at end of the buffer.
 */
uint8_t get_char(char *c) {
  if (gapend == BUFSZ - 1)
    return 1;
  *c = gapbuf[gapbegin++] = gapbuf[++gapend];
  return 0;
}

/*
 * Move the current position
 * pos - position to which to move
 * Returns 0 on success, 1 if pos is invalid
 */
uint8_t jump_pos(uint16_t pos) {
  if (pos > BUFSZ - 1)
    return 1;
  if (pos == GETPOS())
    return 0;
  if (pos > GETPOS())
    do {
      gapbuf[gapbegin++] = gapbuf[++gapend];
    } while (pos > GETPOS());
  else
    do {
      gapbuf[gapend--] = gapbuf[--gapbegin];
    } while (pos < GETPOS());
  return 0;
}

/*
 * Go to next tabstop
 */
uint8_t next_tabstop(uint8_t col) {
  return (col / 8) * 8 + 8;
}

/*
 * Load a file from disk into the gapbuf
 * filename - name of file to load
 * Returns 0 on success
 *         1 if file can't be opened
 *         2 if file too big
 */
uint8_t load_file(char *filename) {
  char c;
  uint8_t i;
  FILE *fp = fopen(filename, "r");
  if (!fp)
    return 1;
  gapbegin = 0;
  gapend = BUFSZ - 1;
  col = 0;
  while (!feof(fp)) {
    c = fgetc(fp);
    switch (c) {
    case '\r': // Native Apple2 files
    case '\n': // UNIX files
      col = 0;
      gapbuf[gapbegin++] = '\r';
      break;
    case '\t':
      c = next_tabstop(col) - col;
      for (i = 0; i < c; ++i)
	gapbuf[gapbegin++] = ' ';
      col += c;
      break;
    default:
      ++col;
      gapbuf[gapbegin++] = c;      
    }
    if (FREESPACE() < 1000) {
      fclose(fp);
      return 2;
    }
    if ((gapbegin % 1000) == 0)
      putchar('.');
  }
  --gapbegin; // Eat EOF character
  fclose(fp);
  return 0;
}

/*
 * Save gapbuf to file
 * filename - name of file to load
 * Returns 0 on success
 *         1 if file can't be opened
 *         2 gapbuf is corrupt
 */
uint8_t save_file(char *filename) {
  char c;
  FILE *fp = fopen(filename, "w");
  if (!fp)
    return 1;
  if (jump_pos(0))
    return 2;
  while (get_char(&c) == 0)
    fputc(c, fp);
  fclose(fp);
  return 0;
}

/*
 * Read next char from gapbuf[] and update state.
 * Returns 1 on EOL, 0 otherwise
 * If do_print is set, print char on the screen.
 * Interface is via the following globals:
 *  do_print  - if 1, update screen
 *  pos       - position in buffer, advanced by one
 *  row, col  - position on screen
 *  rowlen[]  - length of each screen row in chars
 */
uint8_t read_char_update_pos(void) {
  char c;
  if ((c = gapbuf[pos++]) == EOL) {
    if (do_print) {
      rowlen[row] = col + 1;
      putchar(CLREOL);
      putchar('\r');
    }
    ++row;
    col = 0;
    return 1;
  }
  if (do_print)
    putchar(c);
  ++col;
  if (do_print)
    rowlen[row] = col;
  if (col == NCOLS) {
    ++row;
    col = 0;
  }
  return 0;
}

/*
 * Draw screenful of text
 */
void draw_screen(void) {
  uint16_t startpos;
  uint8_t rowsabove, cursorrow;

  // Initialize all rows to length 0 to cover those rows that may have
  // no text and would otherwise be unitialized.
  for (rowsabove = 0; rowsabove < NROWS; ++rowsabove)
    rowlen[rowsabove] = 0;

  // First we have to scan back to work out where in the buffer to
  // start drawing on the screen at the top left. This is at most
  // NROWS * NCOLS chars.
  startpos = gapbegin;
  if (startpos > NROWS * NCOLS)
    startpos -= NROWS * NCOLS;
  else
    startpos = 0;

  // Format the text and work out the number of rows it takes up
  pos = startpos;
  row = col = 0;
  do_print = 0;
  while (pos < gapbegin)
    read_char_update_pos();
  rowsabove = row; // Number of complete rows above cursor

  if (rowsabove <= CURSORROW) {
    pos = 0;
    cursorrow = rowsabove;
  } else {
    cursorrow = CURSORROW;

    // Now repeat the formatting of the text, eating the first
    // (rowsabove - cursorrow) lines
    pos = startpos;
    row = col = 0;
    while (row < (rowsabove - cursorrow))
      read_char_update_pos();
  }

  // Finally actually write the text to the screen!
  videomode(VIDEOMODE_80COL);
  clrscr();
  row = col = 0;
  do_print = 1;
  while (pos < gapbegin)
    read_char_update_pos();

  curscol = col;
  cursrow = row;

  // Now write the text after the cursor up to the end of the screen
  pos = gapend + 1;
  while ((pos < BUFSZ) && (row < NROWS))
    read_char_update_pos();

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Scroll screen up and update rowlen[]
 */
void scroll_up() {
  draw_screen();
}

/*
 * Scroll screen down and update rowlen[]
 */
void scroll_down() {
  draw_screen();
}

/*
 * Update screen after delete_char_right()
 */
void update_after_delete_char_right(void) {
  uint8_t eol = 0;
  uint8_t prevcol;
  col = curscol;
  row = cursrow;
  do_print = 1;

  // Print rest of line up to EOL
  pos = gapend + 1;
  while (!eol && (pos < BUFSZ) && (row < NROWS)) {
    prevcol = col;
    eol = read_char_update_pos();
  }

  // If necessary, print rest of screen
  if ((gapbuf[gapend] == EOL) || (prevcol == NCOLS - 1))
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Update screen after delete_char()
 */
void update_after_delete_char(void) {
  uint8_t eol = 0;
  uint8_t prevcol;
  col = curscol;
  row = cursrow;

  if (gapbuf[gapbegin] == EOL) {
    // Special handling if we deleted an EOL
    if (row > 0)
      col = rowlen[--row] - 1;
    else {
      scroll_up();
      return;
    }
  } else {
    // Erase char to left of cursor & update row, col
    putchar(BACKSPC);
    if (col > 0)
      --col;
    else {
      col = 0;
      if (row > 0)
        --row;
      else {
        scroll_up();
        return;
      }
    }
  }

  curscol = col;
  cursrow = row;
  gotoxy(curscol, cursrow);

  do_print = 1;

  // Print rest of line up to EOL
  pos = gapend + 1;
  while (!eol && (pos < BUFSZ) && (row < NROWS)) {
    prevcol = col;
    eol = read_char_update_pos();
  }

  // If necessary, print rest of screen
  if ((gapbuf[gapbegin] == EOL) || (prevcol == NCOLS - 1))
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

  gotoxy(curscol, cursrow);
  cursor(1);
}


/*
 * Update screen after insert_char()
 */
void update_after_insert_char(void) {
  uint8_t eol = 0;
  uint8_t prevcol;
  col = curscol;
  row = cursrow;

  // Print character just inserted
  pos = gapbegin - 1;
  do_print = 1;
  read_char_update_pos();

  curscol = col;
  cursrow = row;

  if (cursrow == NROWS) {
    scroll_down();
    return;
  }

  // Print rest of line up to EOL
  pos = gapend + 1;
  while (!eol && (pos < BUFSZ) && (row < NROWS)) {
    prevcol = col;
    eol = read_char_update_pos();
  }

  // If necessary, print rest of screen
  if ((gapbuf[gapbegin - 1] == EOL) || (prevcol == 0))
    while ((pos < BUFSZ) && (row < NROWS))
      read_char_update_pos();

  gotoxy(curscol, cursrow);
  cursor(1);
}

/*
 * Move the cursor left
 */
void cursor_left(void) {
  if (gapbegin > 0)
    gapbuf[gapend--] = gapbuf[--gapbegin];
  else {
    putchar(BELL);
    return;
  }
  if (curscol == 0) {
    if (cursrow == 0) {
      scroll_up();
      if (cursrow == 0) {
        putchar(BELL);
        return;
      }
    }
    --cursrow;
    curscol = rowlen[cursrow];
  } else
    --curscol;
  gotoxy(curscol, cursrow);
}

/*
 * Move the cursor right
 */
void cursor_right(void) {
  if (gapbegin < DATASIZE())
    gapbuf[gapbegin++] = gapbuf[++gapend];
  else {
    putchar(BELL);
    return;
  }
  ++curscol;
  if (curscol == rowlen[cursrow]) {
    if (cursrow == NROWS - 1) {
      scroll_down();
      if (cursrow == NROWS - 1) {
        putchar(BELL);
        return;
      }
    }
    ++cursrow;
    curscol = 0;
  }
  gotoxy(curscol, cursrow);
}

/*
 * Move the cursor up
 * Returns 1 if at top, 0 otherwise
 */
uint8_t cursor_up(void) {
  uint8_t i;
  if (cursrow == 0) {
    scroll_up();
    if (cursrow == 0) {
      putchar(BELL);
      gotoxy(curscol, cursrow);
      return 1;
    }
  }
  for (i = 0; i < curscol; ++i)
    gapbuf[gapend--] = gapbuf[--gapbegin];
  --cursrow;
  // Short line ...
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  for (i = 0; i < rowlen[cursrow] - curscol; ++i)
    if (gapbegin > 0)
      gapbuf[gapend--] = gapbuf[--gapbegin];
  gotoxy(curscol, cursrow);
  return 0;
}

/*
 * Move the cursor down
 * Returns 1 if at bottom, 0 otherwise
 */
uint8_t cursor_down(void) {
  uint8_t i;
  if (cursrow == NROWS - 1) {
    scroll_down();
    if (cursrow == NROWS - 1) {
      putchar(BELL);
      return 1;
    }
  }
  if (gapbuf[rowlen[cursrow] - 1] == EOL) {
    for (i = 0; i < rowlen[cursrow] - curscol; ++i) {
      if (gapbegin < DATASIZE()) // TODO Not sure this is necessary now
        gapbuf[gapbegin++] = gapbuf[++gapend];
      else { 
        putchar(BELL);
        return 1;
      }
    }
    ++cursrow;
  }
  // Short line ...
  if (curscol > rowlen[cursrow] - 1)
    curscol = rowlen[cursrow] - 1;
  for (i = 0; i < curscol; ++i)
    if (gapbegin < DATASIZE()) // TODO Not sure this is necessary now
      gapbuf[gapbegin++] = gapbuf[++gapend];
  gotoxy(curscol, cursrow);
  return 0;
}

/*
 * Goto beginning of line
 */
void goto_bol(void) {
  while (curscol > 0)
    cursor_left();  
}

/*
 * Goto end of line
 */
void goto_eol(void) {
  while (curscol < rowlen[cursrow] - 1)
    cursor_right();  
}

/*
 * Jump forward 15 screen lines
 */
void page_down(void) {
  uint8_t i;
  for (i = 0; i < 15; ++i)
    if (cursor_down() == 1)
      break;
}

/*
 * Jump back 15 screen lines
 */
void page_up(void) {
  uint8_t i;
  for (i = 0; i < 15; ++i)
    if (cursor_up() == 1)
      break;
}

/*
 * Delete from cursor to EOL.
 * If at EOL, delete EOL char.
 */
void kill_line(void) {
  // TODO
}

/*
 * Load EMAIL.SYSTEM to $2000 and jump to it
 * (This code is in language card space so it can't possibly be trashed)
 */
#pragma code-name (push, "LC")
void load_email(void) {
  exec("/IP65/EMAIL.SYSTEM", NULL);
}
#pragma code-name (pop)

/*
 * Main editor routine
 */
int edit(char *filename) {
  char c;
  uint16_t pos;
  uint8_t i;
  videomode(VIDEOMODE_80COL);
  if (filename) {
    printf("Loading file %s ", filename);
    if (load_file(filename)) {
      puts("Load error");
      exit(1); // TODO
    }
  }
  jump_pos(0);
  pos = 0;
  draw_screen();
  while (1) {
    cursor(1);
    c = cgetc();
    switch (c) {
    case 0x88:  // OA-Left "Home"
      goto_bol();
      break;
    case 0x8b:  // OA-Up "Page Up"
      page_up();
      break;
    case 0x95:  // OA-Right "End"
      goto_eol();
      break;
    case 0x8a:  // OA-Down "Page Down"
      page_down();
      break;
    case 0x80 + 0x51: // OA-Q "Quit"
    case 0x80 + 0x71: // OA-q
      load_email();
      break;
    case 0x80 + 0x53: // OA-S "Save"
    case 0x80 + 0x73: // OA-s
      save_file(filename);
      break;
    case 0x80 + 0x58: // OA-X "eXit"
    case 0x80 + 0x78: // OA-x
      exit(0);
      break;
    case 0x80 + 0x7f: // OA-Backspace
    case 0x04:  // Ctrl-D "DELETE"
      delete_char_right();
      update_after_delete_char_right();
      break;
    case 0x0c:  // Ctrl-L "REFRESH"
      draw_screen();
      break;
    case 0x7f:  // DEL "BACKSPACE"
      delete_char();
      update_after_delete_char();
      break;
    case 0x09:  // Tab
      c = next_tabstop(curscol) - curscol;
      for (i = 0; i < c; ++i) {
        insert_char(' ');
        update_after_insert_char();
      }
      break;
    case 0x08:  // Left
      cursor_left();
      break;
    case 0x15:  // Right
      cursor_right();
      break;
    case 0x0b:  // Up
      cursor_up();
      break;
    case 0x0a:  // Down
      cursor_down();
      break;
    default:
      //printf("**%02x**", c);
      if ((c >= 0x20) && (c < 0x80) || (c == EOL)) {
        insert_char(c);
        update_after_insert_char();
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc == 2) {
    edit(argv[1]);
  } else
    edit(NULL);
}

