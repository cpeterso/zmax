/*

Here she be, Zmax Code.

This is a full functional, tested, driver. I've attempted to keep
the code as simple as possible and use terms that anyone thats
written any type of protocol knows since its to be used as a guide
to other implementations.

I would suggest that you follow my method of doing the CRCs. You can
figure them as you send each byte or before you send anyting, but my
method is faster since its gets data to the receiver that can be
processed, you can figure the CRCs while thats going on.

*/

/* Include files */

#include<stdio.h>
#include "zmax.h"
#include<malloc.h>
#include<filedata.h>
#include<comm.h>
#include<ctype.h>

/* Globle Declares */

unsigned long UpdCrc32(char byte, unsigned long crc);
long file_position,seof;
int cancel,percent_size,total_errors = 0,endsend = 0,delete_aborted_transfers = 1;
struct filedata filstruc;
long locked_port, atol(), timerset(),amt_sent;
char *Screen_ptr;
char acks_required;
char tbuf[2048],junk_string[81];
int block_len,return_code = 1,port_ptr;
unsigned cur_baud,connect_rate,sending_file;
ASYNC *port;
int     IOadrs = 0x3f8;                      /* comm port base I/O address */
char    irqm = IRQ4;                         /* mask for selected IRQ line */
char    vctrn = VCTR4;                        /* comm interrupt vector nbr */

/* Zmax Structures */

struct Zhead                           /* File info data structure */
{   long flen;                         /* file length */
    unsigned fdate;                    /* Files DATE stamp, DOS format */
    unsigned ftime;                    /* Files TIME stamp, DOS format */
    char fnam[14];                     /* original file name */
}   ;

struct _block_header
  {
   long position;
   unsigned bytes;
   char ack; /* If on (set to 1) the receiver must
                      ACK data block if good. */
  };

struct _info_header
 {
  unsigned flags;
  /* 00000000 00000001 = Can handle Continues Stream Transmissions.

                         If this flag is off, ACK is required following
                         each block of data.

                         ACKING each block of data turns Zmax into a
                         X/Ymodem Batch type protocol.

                         It is suggested that once you reach 128
                         byte blocks, bad lines, Zmax should
                         require each block of data to be ACKED.

                         This  will switch it to a batch
                         Xmodem style protocol which is
                         better for bad phone lines.

                         You'll also notice that although there are
                         provisions for decreasing the block size
                         when noisy lines are encountered, you don't
                         see any place where I raise the block size back
                         up.

                         There is nothing saying you CAN'T do that, I just
                         don't beleive you should. If you encounter
                         enough noise to cause the block size to drop, then
                         the likely hood of you encountering MORE is very high.

                         You'll also notice that I do not drop the block size
                         on the first error. One error does not make for bad
                         phone lines, 2 or more does.

                         */
  int   buf_size;      /* Used to pass transmitter the receivers max
                        buffer size. Its is suggested that the receiver
                        support 2K buffer.

                        The transmitter doesn't have to give you those
                        sizes and can replace them with his own as long
                        as it is less than or equal to your max buffer
                        size.

                        Nor do the block sizes have to be powers of 2.

                        You can use odd block sizes...231, 888, etc.

                        I used powers of 2 because of the way DOS formats
                        harddrives, ie. cluster size.

                        You'll also notice that Zmax places little restrictions
                        on the maximum SIZE of the block although going over
                        2048 shows little gain and lots of loses when a resend
                        is requested.

                        To give you some guidelines for setting block sizes
                        see chart below:

                        Chart based on sending a 60,416 byte file WITH the
                        protocol overhead figured in and driving the serial
                        port to its maximum Cps at that baud rate.

                        Blk Size                     Tx Time
                         Bytes                    2400    9600 LOCKED Port.

                          64                      318.60  79.65
                         128                      285.17  71.29
                         256                      268.45  67.11
                         512                      260.09  65.02
                        1024                      255.91  63.98
                        2048                      253.82  63.46
                        4096                      252.78  63.20
                        8192                      252.26  63.07

                        As you can see, there's quite a bit of savings
                        across the baud rate scale with each increament
                        of the block size up to 2048 bytes. Once you exceed
                        2048 BPB, the savings is almost not measureable.

                        Taking into account the time required to resend 2K of
                        data at 2400 bps, I wouldn't risk going over 1K at 2400.

                        You may REQUEST any size data block up to
                        the size of a signed integer.


                        */
   unsigned char version ; /*

                     This allows for future expansion to the protocol
                     and still yet allow for backwards compatibly.

                     This is the version of the protocol specs that the
                     receiver supports. Currently this should be set to one.

                   */
  char empty[123]; /* Reserves an extra 123 bytes for future expansion */
 };

/* DEFINES */

#define STX 2
#define TSYNCH 0xae
#define ACK 0x06
#define NAK 0x15
#define SOH 0x01
#define EOT 0x04
#define CAN 0x18
#define RESYNC 9
#define PER_WEEK  60480000L
#define PER_DAY    8640000L
#define PER_HOUR    360000L
#define PER_MINUTE    6000L
#define PER_SECOND     100L
#define ENQ 0x05

main(int argc, char *argv[])
{
/*

   This is the main section of this driver. It's makeup is entirely
   up to you. With of course, several REQUIRED sets.

   */
int i,x,count;
int y=0,block_size=1024;
int send=0,do_acks=0;
char s[81],s1[81];
qvideo();
clrscrn();
Screen_ptr = malloc(1 * 4000);
if(Screen_ptr == NULL) exit(1);
uncomprs(&Screen_ptr[0*4000], tmod); /* uncompress screen 1 */
memtoscr(2000, 0, Screen_ptr);
curoff();
for(i=1;i<argc;i++)
 {
  if(argv[i][0] == '/' || argv[i][0] == '-')
  {
    switch(toupper(argv[i][1]))
    {
    case 'S' : block_size = atoi(argv[i+1]);
               if(block_size > 1024)
                             block_size = 1024;
               break;
    case 'A' : do_acks = 1;break;
    case 'K' : delete_aborted_transfers = 0;
               break;
    case 'P' : port_ptr = atoi(argv[i+1]);
               port_ptr--;
               break;
    case 'B' : connect_rate = atoi(argv[i+1]);
               cur_baud = connect_rate;
               block_size = (connect_rate > 2400) ? 2048 : 1024;
               break;
    case 'L' : locked_port = atol(argv[i+1]);
               break;
    case 'R' : set_com();

               /*
                 You should continue to cycle receiving files until
                 Zrecfile returns a ZERO, indicating that it did not
                 receive any files or the session was aborted.
                 */

               while ( Zrecfile(do_acks,block_size) && (async_carrier(port))) clear_screen() ;

               /* Following the last file, the sender will require an ACK
                  to signal the receivers acknowledges end of session.

                  The Sender doesn't respond back after receiving the ACK so
                  continue with you processing after sending a couple of
                  ACKS. Two is suggested, an extra incase line noise blew the
                  first one.
                  */

               com_putc(ACK);
               com_putc(ACK);
               end_prg();
               break;

    case 'F' : set_com();
               i++;
               for(;i<argc;i++)
                {
                 memset(s,'\0',sizeof(s));
                 parser(argv[i],&s[0]);

                 /* Cycle until all files are sent or Zsendfile returns
                    a zero indicating Session was aborted. */

                 if(!Zsendfile(argv[i],s))
                       end_prg();
                 if(!async_carrier(port)) end_prg();
                 clear_screen();
                }
                i = count = 0;

                /* Cycle sending EOT until receiver sees it and ACKs
                    the ending EOT. I suggest cycling only two periods
                    , period being defined as trying ACKEOT function twice. */

                while(!count && i <= 1)
                  {
                   com_putc(EOT);
                   i++;
                   if(ackeot()) count = 1;
                   if(!async_carrier(port)) count = 1;
                  }
                end_prg();
                break;
   }
 }
 }

}
Zsendfile(name,s_name)                      /* transmit a file */
char *name;           /* FULL Drive, path, and name of file to send */
char s_name[]; /*   Only the filename, fit to be used in  file info
                      header.
                 */


{
    long location, blkloc,last_location,t4,size,st,ft,t1,t2,t3;
    int error=0,endblk,x,y,i,cps;
    struct Zhead zero;
    struct _info_header info_header;
    unsigned file_date,file_time,actual;
    char s[80];
    int ok;
    struct _block_header block_header;
    if(!async_carrier(port)) return(0);
    acks_required = 1;
    async_txflush(port);
    file_position = 0L;

         if(!filexist(name)) return(1);

         x = openfil(name,2,&sending_file);
         getstamp(sending_file,&file_date,&file_time);
         memset(&zero,'\0',sizeof(zero)); /* clear out data block */
         filsize(name,&size);
         percent_size = size / 40;
         seof = size;
         strcpy(zero.fnam,s_name);
         zero.fdate = (unsigned) file_date;
         zero.ftime = (unsigned) file_time;
         zero.flen =  (long) size;
         curlocat(5,28);
         colrprtf(0,0,7,"%-14s",zero.fnam);
         curlocat(6,28);
         colrprtf(0,0,7,"%-7ld",size);
         amt_sent = 0L;

         total_errors =  cancel =   0;
         t1 = timerset(6000L);                /* time limit for first block */
         com_putc(TSYNCH);
         com_putc(TSYNCH);
         endsend = i = 0;
         while(x != 'C' && !timeup(t1)) /* Wait for primer start */
          {
           x = com_getc(2);
           if(x != 'C')
               com_putc(TSYNCH);
           if(!async_carrier(port)) return(0);
           if(x == 0x18) cancel++;
           if(cancel > 3)
            {
             curlocat(7,28);
             colrprtf(0,0,7,"%-40s","Rcv Canceled Transfer");
             closefil(sending_file);
             end_prg();
            }
          }
          cancel = 0;
          if(x != 'C')
                goto abort; /* If we did not get it then abort */

          ok = 0;
          while(!get_system_info(&info_header) && async_carrier(port))
            {
            if(++ok > 3)
               async_dtr(port,0); /* After 3 attempts, drop carrier
                                     and bail out.
                                  */
            }
          if(!async_carrier(port)) goto abort;

          block_len = info_header.buf_size;
          if(block_len > 2048)
                       block_len = 2048;
          if( (info_header.flags&0x0001))
                           acks_required = 0;
          cancel = ok = 0;

          curlocat(7,28);
          colrprtf(0,0,7,"%-40s","Sending File Header");
       while(!ok)
       {
          async_rxflush(port);
          SendHeader(&zero);
          switch(wait_ack())
               {
                       case 1:  break;

                       case 2:
                               closefil(sending_file);
                               curlocat(7,28);
                               colrprtf(0,0,7,"%-40s","Rcv Already has File");
                               return(1);
                       case 3:/* REPOS ie. Restart */
                       default: curlocat(7,28);
                                sprintf(junk_string,"BufSize %d (%sStream Mode)",info_header.buf_size,(acks_required) ? "Non-" : "");
                                colrprtf(0,0,7,"%-40s",junk_string);
                                ok = 1;
                                break;

                }/* End switch  */
     }

    st = timerset(0L);
    async_rxflush(port);
    async_txflush(port);
    do
    {
              if( !async_carrier(port) )
                  goto abort;
              if(timeup(t1))
                  goto abort;
              if(checkkey())
                      {
                      i = getkey(&x);
                       if(x == 27)
                         {
                             while(!async_txempty(port)){
                                    if(!async_carrier(port)) break;
                                    }
                             cancel_transfer();
                             goto abort;
                        }
                       }
              while(file_position < zero.flen)/* loop until all sent */
              {
                       if(!async_carrier(port))
                                        goto abort;
                       ChkForNak();
                       if(checkkey())
                       {
                         i = getkey(&x);
                         if(x == 27)
                         {
                             while(!async_txempty(port)){
                                    if(!async_carrier(port)) break;
                                    }
                             cancel_transfer();
                             goto abort;
                        }
                       }
                       i = seekfil(sending_file,0,file_position,&location);
                       i = readfil(sending_file,block_len,tbuf,&actual);
                       last_location = block_header.position = file_position;
                       block_header.bytes = actual;
                       block_header.ack = (acks_required) ? 1 : 0 ;
                       file_position = (long) file_position + (long) actual;
                       amt_sent = (long) file_position;
                       com_putc(SOH);
                       com_putc(ENQ);
                       send_block_header(&block_header);
                       SendDataBlock(tbuf,actual);
                       if(block_header.ack)
                        {
                          i = com_getc(10);
                          if(i != ACK){
                                file_position = last_location;
                                curlocat(7,28);
                                sprintf(junk_string,"REPOS %ld",file_position);
                                colrprtf(0,0,7,"%-40s",junk_string);
                                curlocat(5,58);
                                colrprtf(0,0,7,"%d",++total_errors);
                                }
                        }
                       else
                          ChkForNak();
                       if(file_position == zero.flen)
                       {
                           com_putc(EOT);
                           endsend = 0;
                           t1 = timerset(9000L);  /* time limit for last block if ackless(90 secs) */
                           sprintf(junk_string,"%-7ld",zero.flen);
                           curlocat(6,58);
                           colrprtf(0,0,7,"%s",junk_string);
                           for(i=1;i<=40;i++)
                           {
                              curlocat(19,(21+i));
                              colrprtf(0,15,7,"Û");
                           }
                        }
                        ChkForNak();
                        if(cancel >= 5) goto abort;
              }/* Still to go */
              ChkForNak();
              if(cancel >= 5) goto abort;

    }while(!endsend);
    /* end main send */

    ft = timerset(0L);
    closefil(sending_file);
      ft = (long) (ft-st) / 100L;
      if(ft == 0) ft = 1L;
      cps = (int) (zero.flen / ft);
      x = (cps*10) / (connect_rate/100);
      curlocat(14,28);
      colrprtf(0,0,7,"%3d%%",x);
      return_code = 0;
      return(1);                          /* exit with good status */

abort:
    async_txflush(port);
    closefil(sending_file);
    return(0);                          /* exit with bad status */
}


ChkForNak()                        /* check for bad block */
{
    unsigned char c=0;
    int q;
    struct _block_header block_header;

    top:;
      c = com_getc(0);
      if(c == 'C' || c == ACK)
       {
        if(c == ACK)
             {
             if(file_position == seof)
                              endsend = 1;
             return;
             }
        q = com_getc(0);
        if(q != '*')
                  return;
        if(get_block_header(&block_header))
        {
          file_position = block_header.position;
          curlocat(7,28);
          sprintf(junk_string,"REPOS %ld",file_position);
          colrprtf(0,0,7,"%-40s",junk_string);
          curlocat(5,58);
          colrprtf(0,0,7,"%d",++total_errors);
          if(total_errors > 1 && block_len > 128)
               block_len >>= 1;
         if(block_len == 128 && !acks_required)
                           acks_required = 1;
        }
     }
      else
       if(c == CAN)
             cancel++;

if( (async_rxcnt(port)))
       goto top;
}

SendDataBlock(blk,size)         /* physically ship a block */
char *blk;                             /* data to be shipped */
int size;/* size of block */
{
register i,x,y;
unsigned long oldcrc;
long location;
static unsigned char ch;
char *b = blk;
char *j = blk;
static int n,q,flow,aa,kk;


          while(  (async_txblk(port, b, size)) == R_TXERR)
          {
                curlocat(6,58);
                location = (long) file_position - size;
                colrprtf(0,0,7,"%-7ld",location);
                kk = location / percent_size;
                if(kk > 40) kk = 40;
                for(aa=1;aa<=kk;aa++)
                           {
                              curlocat(19,(21+aa));
                              colrprtf(0,15,7,"Û");
                           }
                if(async_rxcnt(port))
                {
                  flow = (async_peek(port,0)&0xff);
                  if(flow == 'C')
                   {
                      async_txflush(port);
                      return;
                   }
                   else
                     if(flow != CAN)
                          flow = async_rx(port); /* Eat non C/CAN char, more
                                                    than likely was line noise
                                                     */
                }
                else
                  if(!async_carrier(port)) return;

         }/* end while*/
          oldcrc = 0xFFFFFFFF;
          for(n=0;n<size;n++){
                  oldcrc = UpdCrc32(((unsigned short) (*j++)), oldcrc);
                  }
          oldcrc = ~oldcrc;
        for(n=4; --n >=0;)
        {
          com_putc((unsigned char) oldcrc);
           oldcrc >>= 8;
        }
        if(acks_required)
        {
                curlocat(6,58);
                location = (long) file_position - size;
                colrprtf(0,0,7,"%-7ld",location);
                kk = location / percent_size;
                if(kk > 40) kk = 40;
                for(aa=1;aa<=kk;aa++)
                           {
                              curlocat(19,(21+aa));
                              colrprtf(0,15,7,"Û");
                           }
        }
}

ackeot()
{
long t;
int c;
t = timerset(300L);
while(!timeup(t))
{
c = com_getc(0);
    if(c == ACK)
            return(1);
}

return(0);
}
long timerset (t)
long t;
{
        long l;
        int l2;
        int hours,mins,secs,ths,year,month,day;

        sysitime (&hours,&mins,&secs,&ths);
        sysidate(&year,&month,&day);
        l2 = dayofwee(year,month,day);

        l =     l2      * PER_DAY       +
                hours   * PER_HOUR      +
                mins    * PER_MINUTE    +
                secs    * PER_SECOND    +
                ths                     ;

        l += t;

        return (l);
        }

timeup (t)
long t;
{
        long l;

        l = timerset (0L);
        if(l < (t-PER_DAY)) l = l + PER_DAY;
        return ((l - t) >= 0L);
        }

com_getc(t)
int t;
{
        static long t1,t2;
        int c;
        if(!((c = async_rx(port)) & B_RXEMPTY))
               return((c&0xff));
        t2 = (long) t * 100L;
        t1 = timerset (t2);
        while(((c = async_rx(port)) & B_RXEMPTY))
                {
                if(!async_carrier(port))
                    {
                    return(EOF);
                    }
                if (timeup(t1))
                        {
                        return(EOF);
                        }
                }
        return((c&0xff));


        }
wait_ack()
{
int ch;
long t;
struct _block_header block_header;

t = timerset(3000L); /* If no rsponds in 30 seconds, skip file */
for(;;)
 {
    ch = com_getc(5);
    if(ch == 'D')
      {
            ch = com_getc(5);
            if(ch == 'P')
            {
               com_putc(ACK);
               return(2);
            }
      }/* End if Duplicate File */

    if(ch == ACK) return(4);

   if(ch == CAN || timeup(t))
    {
       async_rxflush(port);
       return(2);
    }

    if(ch == RESYNC)
    {
      if(get_block_header(&block_header))
        {
          file_position = block_header.position;
          return(3);
        }

    }/* Resync */

   if(ch == 'C')
         return(1);
 }

}

SendHeader(blk)                      /* physically ship Fileinfo block */
char *blk;                             /* data to be shipped */
{
register ch,n;
unsigned long oldcrc;                  /* CRC check value */

    com_putc(SOH);
    com_putc(NAK);

    while(  (async_txblk(port, blk, 22)) == R_TXERR);

         oldcrc = 0xFFFFFFFF;
          for(n=0;n<22;n++){
                  oldcrc = UpdCrc32(((unsigned short) (*blk++)), oldcrc);
                  }
          oldcrc = ~oldcrc;
        for(n=4; --n >=0;)
        {
          com_putc((unsigned char) oldcrc);
           oldcrc >>= 8;
        }

}

set_com()
{
char parms[80],s2[30],s3[30];
int ch;
long atol();
if(port_ptr)
   {
   IOadrs = 0x2f8;
   irqm = 0x08;
   vctrn = 11;
   }
port = malloc(sizeof(ASYNC));
sprintf(s3,"COM%d",port_ptr+1);
memset(s2,'\0',sizeof(s2));
if(!envget2(s3,s2))
    locked_port = (long) atol(s2);
if(locked_port)
sprintf(parms,"%ldN81",locked_port);
else
sprintf(parms,"%dN81",cur_baud);
if(!(AllocRingBuffer(port, 2048, 3072, 1)))/* Note that I allocate
                                              a TX buffer 1K larger
                                              than the largest block
                                              size that I support.

                                              This gives me a little
                                              breathing space in STREAM
                                              mode at 9600+ for file management. */
   exit(0);
if ((ch = async_open(port, IOadrs, irqm, vctrn, parms)) != R_OK)
          exit(0);
async_rxflush(port);
async_txflush(port);
async_msrflow(port, B_CTS);
async_FIFOtxlvl(port,4);
async_FIFOrxlvl(port,1);
}

parser(s,name) char s[],name[];
{
char stack[80];
int i,x;
if(strindex("\\",s) == -1)
 {
  name[0] = 0;
  strcpy(name,s);
  return;

 }

memset(stack,'\0',sizeof(stack));
x = strlen(s);
i = 0;
while(s[x] != '\\' && x > 0)
  {
  if(s[x] != 0 && s[x] != 13 && s[x] != 10) stack[i++] = s[x];
  x--;
  }
x = 0;
i--;
while(i >= 0)
   name[x++] = stack[i--];
name[x] = '\0';
}

end_prg()
{
curon();
if(async_carrier(port))
     port->OldMCR |= B_DTR;
async_close(port);
if(!delete_aborted_transfers)
  sleep(20);
exit(return_code);

}

Zrecfile(int do_acks,int block_size)                    /* receive file */
{
    int n,w,x,y,tries,i,c,cps;
    char outname[81];
    int kk,aa;
    unsigned send,actual,file_date,file_time;
    struct Zhead zero;                 /* file header data storage */
    struct _block_header block_header;
    struct _info_header SystemInfo;
    long t1,st,ft,lsize,testl;

    tries = total_errors = c = 0;
    percent_size = 32000;
    memset(outname,'\0',sizeof(outname));
    memset(&zero,'\0',sizeof(zero));
    amt_sent = 0L;
    file_position = 0L;
    st = timerset(6000L);
    com_putc(' ');

    while(!timeup(st))
    {
         if(!async_carrier(port))
                          return(0);
     c = com_getc(1);
     if(c == TSYNCH)
         st =timerset(0L);
     if(c == EOT)
         return(0);
    }
    if(c != TSYNCH)
     {
         curlocat(7,28);
         colrprtf(0,0,7,"%-40s","Sender flobbed it");
         st = timerset(500L);
         while(!timeup(st)) ;
         return(0);
     }

    cancel = c = 0;
    st = 0L;
    com_putc('C');
    c = '\0';
    memset(&SystemInfo,'\0',sizeof(SystemInfo));
    if(!do_acks)
       SystemInfo.flags ^= 0x0001;
    SystemInfo.buf_size =  block_size;
    SystemInfo.version = 1;

    send_system_info(&SystemInfo);

    curlocat(7,28);
    colrprtf(0,0,7,"%-40s","Waiting For File");

    goto nextblock;

badblock:                              /* we got a bad block */

    if(++tries>4)
    {
         async_dtr(port,0);
         sleep(3);
         async_dtr(port,1);
         goto abort;
    }
    if(!block_header.ack)
    {
    async_rxflush(port);
    com_putc('C');
    com_putc('*');
    block_header.position = file_position;
    send_block_header(&block_header);
    async_rxflush(port);
    }
    else
      com_putc('C');
    i = seekfil(send,2,0L,&testl);
    curlocat(7,28);
    sprintf(junk_string,"REPOS %ld",file_position);
    colrprtf(0,0,7,"%-40s",junk_string);
    curlocat(5,58);
    colrprtf(0,0,7,"%d",++total_errors);
    goto nextblock;

goodblock:                              /* we got a good block */

    if(block_header.ack)
                 com_putc(ACK);
    amt_sent = (long) file_position;

nextblock:                             /* start of "get a block" */
    t1 = timerset(2000L);
    while(!timeup(t1))
    {
         if(checkkey())
         {
          aa = getkey(&kk);
          if(kk == 27)
           {
            cancel_transfer();
            goto abort;
           }
         }
         c = (com_getc(3)&0xff);
         switch (c)
         {
         case CAN : cancel++;
                    if(cancel >= 5)
                           goto abort;
                    break;
         case SOH :
                        x = com_getc(5);
                        switch(x)
                           {
                              case NAK :
                                            if(readblock(&zero,22))
                                              {
                                                     st = timerset(0L);
                                                     sprintf(outname,"%s",zero.fnam);
                                                     if(!filexist(outname))
                                                                 com_putc(ACK);
                                                        if(filexist(outname))
                                                          {
                                                             openfil(outname,2,&send);
                                                             getstamp(send,&file_date,&file_time);
                                                             closefil(send);
                                                             filsize(outname,&lsize);
                                                             if(lsize < zero.flen && zero.fdate == file_date && zero.ftime == file_time)
                                                                {
                                                                     openfil(outname,2,&send);
                                                                     seekfil(send,2,0L,&file_position);
                                                                     curlocat(7,28);
                                                                     sprintf(junk_string,"Re-Start: %ld",file_position);
                                                                     colrprtf(0,0,7,"%-40s",junk_string);
                                                                     com_putc(RESYNC);
                                                                     block_header.position = file_position;
                                                                     send_block_header(&block_header);
                                                                 }
                                                                 else
                                                                 {
                                                                      if(lsize == zero.flen && zero.fdate == file_date && zero.ftime == file_time)
                                                                       {
                                                                          x = i = 0;
                                                                          curlocat(7,28);
                                                                          colrprtf(0,0,7,"%-40s","Duplicate: Asking for Skip");
                                                                          async_rxflush(port);
                                                                          com_putc('D');
                                                                          com_putc('P');
                                                                          c = com_getc(5);
                                                                          if(c == ACK)
                                                                               return(1);

                                                                          return(0);
                                                                       }

                                                                 }
                                                         } /* end if exits */
                                                         else
                                                             i = creatfil(outname,0,&send);
                                                         curlocat(5,28);
                                                         colrprtf(0,0,7,"%-14s",outname);
                                                         curlocat(6,28);
                                                         colrprtf(0,0,7,"%-7ld",zero.flen);
                                                         percent_size  = zero.flen / 40;
                                                         curlocat(7,28);
                                                         colrprtf(0,0,7,"%-40s","Getting File");

                                                         goto goodblock;
                                               }
                                                curlocat(7,28);
                                                colrprtf(0,0,7,"%-40s","Bad File Header");
                                                com_putc('C');
                                                goto nextblock;           /* bad header block */
                                        break;
                              case ENQ :memset(&block_header,'\0',sizeof(block_header));
                                        if(get_block_header(&block_header))
                                        {
                                          if(block_header.position != file_position)
                                           {
                                                goto badblock;
                                           }
                                           if(readblock(&tbuf[0],block_header.bytes))      /* else if we get it okay */
                                              {
                                                 n = writefil(send,block_header.bytes,tbuf,&actual);
                                                 file_position = (long) file_position + (long) actual;
                                                 curlocat(6,58);
                                                 colrprtf(0,0,7,"%-7ld",file_position);
                                                 tries = 0;
                                                 kk = file_position / percent_size;
                                                 if(kk > 40) kk = 40;
                                                 for(aa=1;aa<=kk;aa++)
                                                  {
                                                     curlocat(19,(21+aa));
                                                     colrprtf(0,15,7,"Û");
                                                  }
                                                 goto goodblock;
                                               }
                                                goto badblock;
                                        }
                                        break;
                            }/* end switch x*/
                         break;
         case EOT :
                    if(file_position == zero.flen || file_position == 0)
                    {
                                com_putc(ACK);
                                if(file_position == 0) return(0);
                                ft = timerset(0L);
                                sprintf(junk_string,"%-7ld",zero.flen);
                                curlocat(6,58);
                                colrprtf(0,0,7,"%s",junk_string);
                                closefil(send);
                                openfil(outname,2,&send);
                                setstamp(send,zero.fdate,zero.ftime);
                                closefil(send);
                                 for(i=1;i<=40;i++)
                                  {
                                    curlocat(19,(21+i));
                                   colrprtf(0,15,7,"Û");
                                  }
                                  ft = (long) (ft - st) /100L;
                                  if(ft == 0) ft = 1L;
                                  cps = (int) (zero.flen / ft);
                                  i = (connect_rate/100);
                                  if(i<=0)i=1;
                                  x = (cps*10) / i;
                                  curlocat(14,28);
                                  return_code = 0;
                                  colrprtf(0,0,7,"%3d%%",x);
                                  return(1);

                    }
                    break;
         }
    if(!async_carrier(port)){
         goto abort;
         }
   }
   goto badblock;
abort:
  async_rxflush(port);
  if(file_position > 0 )
   {
    closefil(send);
    openfil(outname,2,&send);
    setstamp(send,zero.fdate,zero.ftime);
    closefil(send);
    if(delete_aborted_transfers)
                    unlink(outname);
   }
   else
       return(0);
}


readblock(buf,size)             /* read a block of data */
char *buf;                             /* data buffer */
int size;
{
    register n;
    unsigned long crc;            /* CRC check values */
    long t1;  /* short block timeout */
    int x,c;
    char *p = buf;
    char *s = buf;

    n = 0;
    c = size;

    while(n<size)
    {
           x = async_rxblk(port,s,c);
           n+=x,c-=x,s+=x;
           if(!(async_rxcnt(port)) && n<size)
             {
                 t1 = timerset(1000L);
                 while(!timeup(t1) && !async_rxcnt(port))
                  {
                     if(!async_carrier(port)) return(0);
                 }
                if(timeup(t1)) return(0);
             }/* end if */

    } /* end while n<size */
      crc = 0xFFFFFFFF;
      for(n=0;n<size;n++){
                  crc = UpdCrc32(((unsigned short) (*p++)), crc);
                  }

       for (n = 4; --n >= 0;)
       {
         c = com_getc(2);
         crc = UpdCrc32(c, crc);
       }
   if (crc != 0xDEBB20E3)
      {
      return(0);
      }
      return(1);
}


com_putc(int t)
{

    if(!async_carrier(port)) return;

    while((async_tx(port,t)) == R_TXERR)
    {
      if(!async_carrier(port)) return;
    }


}

sleep(delay)
int delay;
{
long a;
a = timerset( (long) (delay * 10L) );
while(!timeup(a)) ;
}

clear_screen()
{
int x;
curlocat(4,28); colrprtf(0,0,7,"%-19s","");
curlocat(5,28); colrprtf(0,0,7,"%-14s","");
curlocat(6,28); colrprtf(0,0,7,"%-8s","");
curlocat(7,28); colrprtf(0,0,7,"%-40s","");
curlocat(5,58); colrprtf(0,0,7,"%-3s","");
curlocat(6,58); colrprtf(0,0,7,"%-7s","");
for(x=1;x<=40;x++)
 {
  curlocat(19,(21+x));
    colrprtf(0,0,7,"²");
 }
}

get_block_header(char *buf)
{
    register n;
    unsigned long crc;
    int c,x;
    long t1;
    char *s = buf;
    char *p = buf;

    n = 0;
    c = 7;

    while(n<7)
    {
           x = async_rxblk(port,s,c);
           n+=x,c-=x,s+=x;
           if(!(async_rxcnt(port)) && n<7)
             {
                 t1 = timerset(1000L);
                 while(!timeup(t1) && !async_rxcnt(port))
                  {
                     if(!async_carrier(port)) return(0);
                  }
                if(timeup(t1)) return(0);
             }/* end if */

    } /* end while n<7 */
      crc = 0xFFFFFFFF;
      for(n=0;n<7;n++){
                  crc = UpdCrc32(((unsigned short) (*p++)), crc);
                  }

       for (n = 4; --n >= 0;)
       {
         c = com_getc(2);
         crc = UpdCrc32(c, crc);
       }

   if (crc != 0xDEBB20E3)
                    return(0);

return(1);

}

send_block_header(char *b)
{
register n;
unsigned long oldcrc;
while(  (async_txblk(port, b, 7)) == R_TXERR) ;

          oldcrc = 0xFFFFFFFF;
          for(n=0;n<7;n++){
                  oldcrc = UpdCrc32(((unsigned short) (*b++)), oldcrc);
                  }
          oldcrc = ~oldcrc;
          for(n=4; --n >=0;)
           {
              com_putc((unsigned char) oldcrc);
              oldcrc >>= 8;
           }
}

get_system_info(char *buf)
{
    register n;
    unsigned long crc;
    int c,x;
    long t1;
    char *s = buf;
    char *p = buf;
curlocat(7,28);
colrprtf(0,0,7,"%-40s","Waiting For System Information");

    n = 0;
    c = 128;

    while(n<128)
    {
           x = async_rxblk(port,s,c);
           n+=x,c-=x,s+=x;
           if(!(async_rxcnt(port)) && n<128)
             {
                 t1 = timerset(1000L);
                 while(!timeup(t1) && !async_rxcnt(port))
                  {
                     if(!async_carrier(port)) return(0);
                  }
                if(timeup(t1)) return(0);
             }/* end if */

    } /* end while n<128 */
      crc = 0xFFFFFFFF;
      for(n=0;n<128;n++){
                  crc = UpdCrc32(((unsigned short) (*p++)), crc);
                  }

       for (n = 4; --n >= 0;)
       {
         c = com_getc(2);
         crc = UpdCrc32(c, crc);
       }

   if (crc != 0xDEBB20E3){
             com_putc('C');
             return(0);
             }

com_putc(ACK);
return(1);
}

send_system_info(char *b)
{
register n;
int c,attempts=0;
unsigned long oldcrc;
char *p;
curlocat(7,28);
colrprtf(0,0,7,"%-40s","Sending System Information");
async_rxflush(port);
while(async_rxcnt(port)){
          async_rxflush(port);
          }
for(;;)
{

         while(  (async_txblk(port, b, 128)) == R_TXERR) ;

          oldcrc = 0xFFFFFFFF;
          p = b;
          for(n=0;n<128;n++){
                  oldcrc = UpdCrc32(((unsigned short) (*p++)), oldcrc);
                  }
          oldcrc = ~oldcrc;
          for(n=4; --n >=0;)
           {
              com_putc((unsigned char) oldcrc);
              oldcrc >>= 8;
           }
           c = com_getc(5);
           if(c == ACK) return;
           if(++attempts > 3)
           {
                async_dtr(port,0);
                sleep(3);
                async_dtr(port,1);
                return;
           }/* Screw it, drop connection and try later */

}/* End send system info */

}
cancel_transfer()
{

/* only 5 CAN's needed to abort a session, but I like
   to send more thans actual needed. */

com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
com_putc(CAN);
while(!async_txempty(port))
       if(!async_carrier(port)) break;
}
