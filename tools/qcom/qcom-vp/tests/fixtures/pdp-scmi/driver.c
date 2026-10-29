// SPDX-License-Identifier: BSD-3-Clause-Clear
// Synthetic responses test observer admission only. They do not authenticate an image.
#include <stdint.h>
#ifndef CASE
#define CASE 0
#endif

#define RX 0x17610000u
#define GIC 0x17000000u
#define RING 0x02000000u
#define REQUEST (RING + 0x2800u)
#define CONTEXT 0x400000u
#define INPUT 0x400100u
#define CPUCP 0x18b4e200u
#define BELL 0x494d4353u
static volatile uint32_t *words(uint64_t address) { return (volatile uint32_t *)(uintptr_t)address; }
static void put(uint64_t address, uint32_t value) { *words(address)=value; }
static uint32_t get(uint64_t address) { return *words(address); }
static void barrier(void) { __asm__ volatile("dsb sy\n\tisb" ::: "memory"); }
__attribute__((noreturn)) static void stop(uint32_t marker) {
    put(marker, 0);
    for (;;) __asm__ volatile("wfe");
}
static void check(int value) { if (!value) stop(0xbad00000u); }
static uint64_t slot(unsigned endpoint) { return 0x81f08100ull+endpoint*0x80; }
static uint64_t tx(unsigned endpoint) { return endpoint ? 0x1a980000u : 0x19980000u; }
static uint32_t sentinel(unsigned endpoint, unsigned index) {
    return 0xa5100000u | (endpoint<<12) | index;
}
static void slots_unchanged(int admitted) {
    for (unsigned e=0;e<2;e++)
        for (unsigned i=0;i<32;i++)
            check(get(slot(e)+4*i)==((admitted && i==1) ? 1 : sentinel(e,i)));
    for (unsigned i=0;i<32;i++) check(get(CPUCP+4*i)==(0xc0c00000u+i));
}
static void cpucp_unchanged(void) {
    for (unsigned i=0;i<32;i++) check(get(CPUCP+4*i)==(0xc0c00000u+i));
}

static unsigned sequence;
// Each publication contains fresh request bytes and fresh completion bytes.
// The guest acts as both ring producer and synthetic firmware responder.
static uint64_t publish(unsigned opcode, unsigned image, unsigned length, unsigned operation) {
    unsigned n=++sequence;
    uint64_t request=REQUEST+16+((n-1)%2)*20;
    put(INPUT,image);
    put(INPUT+4,operation);
    put(request,n); put(request+4,opcode); put(request+8,INPUT);
    put(request+12,length);
    put(request+16,0);
    put(REQUEST+8,n);
    return request;
}
static void complete(uint64_t request, unsigned opcode, unsigned status, unsigned release) {
    uint64_t response=RING+16+((sequence-1)%2)*20;
    put(response,sequence); put(response+4,opcode); put(response+8,2);
    put(response+12,status);
    put(RING+8,sequence);
    if (opcode==0x10 || opcode==0x12) put(CONTEXT,0);
    // No admission is allowed before the consumer's release store.
    slots_unchanged(0);
    put(request+16,release);
    put(REQUEST+12,sequence);
    put(RING+12,sequence);
}
static void phase(unsigned opcode, unsigned status, unsigned release) {
    put(CONTEXT,0x91);
    unsigned length=opcode==6 ? 0xa4 : opcode==0x10 ? 0x59 : opcode==0x11 ? 0x10 : 1;
    uint64_t request=publish(opcode,opcode==0x12 ? 1 : 0x91,length,0);
    complete(request,opcode,status,release);
}
static void stream(unsigned operation) {
    put(CONTEXT,0x91);
    uint64_t request=publish(7,0x91,0x1c,operation);
    complete(request,7,0,1);
}
static void admission(void) {
    put(RING,0x57464d54); put(RING+4,2); put(RING+8,0); put(RING+12,0);
    put(REQUEST,0x43535841); put(REQUEST+4,2); put(REQUEST+8,0); put(REQUEST+12,0);
    // A combined command requires its own valid metadata lineage and matching image.
    for (unsigned bad=0;bad<7;bad++) {
        phase(6,bad==0 ? 1 : 0,1);
        put(CONTEXT,bad==3 ? 0x21 : 0x91);
        uint64_t request=publish(0x10,bad==4 ? 0x21 : 0x91,bad==5 ? 0x58 : 0x59,0);
        complete(request,bad==6 ? 0x11 : 0x10,bad==1 ? 1 : 0,bad==2 ? 0 : 1);
        slots_unchanged(0);
        phase(0x10,0,1);
        slots_unchanged(0);
    }
    // Start, update, no-op, failed, and mismatched streaming requests cannot finish a segment.
    for (unsigned bad=0;bad<6;bad++) {
        phase(6,0,1); stream(1); stream(2);
        if (bad) {
            put(CONTEXT,0x91);
            uint64_t request=publish(7,bad==3 ? 0x21 : 0x91,bad==4 ? 0x1b : 0x1c,
                                     bad==2 ? 4 : bad==5 ? 5 : 3);
            complete(request,7,bad==1 ? 1 : 0,1);
        }
        phase(0x12,0,1);
        slots_unchanged(0);
    }
    // A failed metadata, segment, finalization, or release invalidates its lineage.
    // The later stages must not recover admission from a failed earlier stage.
    for (unsigned bad=0;bad<4;bad++) {
        phase(6,bad==0 ? 1 : 0,1);
        phase(0x11,bad==1 ? 1 : 0,1);
        phase(0x12,bad==2 ? 1 : 0,bad==3 ? 0 : 1);
        slots_unchanged(0);
    }
#if CASE == 7
    phase(6,0,1); phase(0x10,0,1);
#elif CASE == 8
    phase(6,0,1); stream(1); stream(2); stream(3); phase(0x12,0,1);
#else
    phase(6,0,1); phase(0x11,0,1); phase(0x12,0,1);
#endif
    slots_unchanged(1);
}

static unsigned token;
static uint32_t begin(unsigned endpoint, unsigned protocol, unsigned message,
                      unsigned count, uint32_t a, uint32_t b, uint32_t c) {
    uint64_t s=slot(endpoint);
    uint32_t header=((++token & 0x3ff)<<18)|(protocol<<10)|message;
    put(s+4,0); put(s+16,1); put(s+20,4+count*4); put(s+24,header);
    if (count>0) put(s+28,a);
    if (count>1) put(s+32,b);
    if (count>2) put(s+36,c);
    barrier();
    put(tx(endpoint)+0x104,BELL);
    barrier();
    check(get(s+4)==1 && get(s+24)==header);
    check(get(RX+0x104+endpoint*8)==BELL);
    check(get(s)==sentinel(endpoint,0));
    check(get(s+8)==sentinel(endpoint,2) && get(s+12)==sentinel(endpoint,3));
    check(get(s+16)==1);
    check(get(s+20)>=8 && get(s+20)<=104);
    cpucp_unchanged();
    return get(s+28);
}
static void clear(unsigned bits) { put(RX+0x4800,bits); barrier(); }
static void pending(unsigned expected) {
    for (unsigned n=0;n<10000;n++) {
        barrier();
        if ((get(GIC+0x208)&4)==(expected ? 4u : 0u)) return;
    }
    stop(0xbad00000u);
}
static void no_irq(void) {
    // A GIC may retain a latched pending bit after the level source falls.
    put(GIC+0x288,4); barrier(); pending(0);
}
static uint64_t call(unsigned endpoint, unsigned protocol, unsigned message,
                     unsigned count, uint32_t a, uint32_t b, uint32_t c, int32_t status) {
    check(begin(endpoint,protocol,message,count,a,b,c)==(uint32_t)status);
    if (status) check(get(slot(endpoint)+20)==8);
    check(get(RX+0x4400)==(1u<<endpoint));
    clear(1u<<endpoint);
    check(get(RX+0x4400)==0);
    return slot(endpoint)+32;
}
static uint64_t perf(unsigned e,unsigned m,unsigned count,uint32_t a,uint32_t b,uint32_t c,int32_t status) {
    return call(e,0x13,m,count,a,b,c,status);
}
static void state(unsigned endpoint,unsigned domain,unsigned maximum,unsigned minimum,unsigned level) {
    uint64_t r=perf(endpoint,6,1,domain,0,0,0);
    check(get(r)==maximum && get(r+4)==minimum);
    r=perf(endpoint,8,1,domain,0,0,0);
    check(get(r)==level);
}
static void services(unsigned e) {
    uint64_t r=call(e,0x10,0,0,0,0,0,0); check(get(r)==0x20000);
    r=call(e,0x10,1,0,0,0,0,0); check(get(r)==0x101);
    for (unsigned m=0;m<=6;m++) {
        r=call(e,0x10,2,1,m,0,0,0); check(get(r)==0);
    }
    call(e,0x10,2,1,7,0,0,-1);
    // Identity fields must fit their 16-byte wire fields and remain terminated.
    for (unsigned m=3;m<=4;m++) {
        r=call(e,0x10,m,0,0,0,0,0);
        check(get(slot(e)+20)==24 && get(r)!=0);
        check(((volatile unsigned char *)(uintptr_t)r)[15]==0);
    }
    r=call(e,0x10,5,0,0,0,0,0); check(get(r)==1);
    unsigned skip=0;
    for (unsigned page=0;page<2;page++) {
        r=call(e,0x10,6,1,skip,0,0,0);
        unsigned count=get(r); check(count<=1);
        if (!page) { check(count==1 && get(r+4)==0x13); skip+=count; }
        else check(count==0 && skip==1);
    }
    call(e,0x10,6,1,2,0,0,-2);
    r=perf(e,0,0,0,0,0,0); check(get(r)==0x40000);
    r=perf(e,1,0,0,0,0,0); check(get(r)==2);
    check(get(r+4)==0 && get(r+8)==0 && get(r+12)==0);
    for (unsigned m=0;m<=8;m++) {
        r=perf(e,2,1,m,0,0,0); check(get(r)==0);
    }
    perf(e,2,1,11,0,0,-1);
    for (unsigned domain=0;domain<2;domain++) {
        r=perf(e,3,1,domain,0,0,0);
        check(get(r)==0xc0000000u && get(r+4)==0 && get(r+8)==1000000 && get(r+12)==0);
        check(((volatile unsigned char *)(uintptr_t)(r+16))[15]==0);
        unsigned start=0,remaining=2;
        for (unsigned page=0;page<2;page++) {
            r=perf(e,4,2,domain,start,0,0);
            unsigned flags=get(r),count=flags&0xffff,next=flags>>16;
            check(count==1 && next==remaining-count && get(slot(e)+20)==32);
            check(get(r+4)==start && get(r+8)==0 && get(r+12)==30);
            check(get(r+16)==(start+1)*1000000 && get(r+20)==start);
            start+=count; remaining=next;
        }
        check(start==2 && remaining==0);
        perf(e,4,2,domain,2,0,-5);
        state(e,domain,1,0,0);
        perf(e,7,2,domain,1,0,0);
        state(1-e,domain,1,0,1); // Both endpoints see the same retained state.
        perf(e,5,3,domain,0,0,0); // Clamp down.
        state(e,domain,0,0,0);
        perf(e,7,2,domain,1,0,-5);
        state(1-e,domain,0,0,0);
        perf(e,5,3,domain,1,1,0); // Clamp up.
        state(e,domain,1,1,1);
        perf(e,7,2,domain,0,0,-5);
        state(1-e,domain,1,1,1);
        perf(e,5,3,domain,0,1,-5);
        state(1-e,domain,1,1,1);
        perf(e,5,3,domain,2,0,-5);
        state(1-e,domain,1,1,1);
        perf(e,7,2,domain,2,0,-5);
        state(1-e,domain,1,1,1);
        perf(e,7,1,domain,0,0,-2); // Valid transport, short service payload.
        state(1-e,domain,1,1,1);
        perf(e,5,2,domain,0,0,-2);
        state(1-e,domain,1,1,1);
        for (unsigned m=9;m<=11;m++) {
            perf(e,m,1,domain,0,0,-1);
            state(1-e,domain,1,1,1);
        }
        call(e,0x80,0,0,0,0,0,-1);
        state(1-e,domain,1,1,1);
        call(e,0x14,0,0,0,0,0,-1);
        state(1-e,domain,1,1,1);
        call(e,0x16,0,0,0,0,0,-1);
        state(1-e,domain,1,1,1); // All refusals leave state intact.
        perf(e,5,3,domain,1,0,0);
        perf(e,7,2,domain,0,0,0);
        state(e,domain,1,0,0);
    }
    perf(e,3,1,2,0,0,-4);
    state(e,0,1,0,0); state(e,1,1,0,0);
    perf(e,7,2,2,1,0,-4);
    state(e,0,1,0,0); state(e,1,1,0,0);
    perf(e,5,3,2,0,0,-4);
    state(e,0,1,0,0); state(e,1,1,0,0);
}
void pdp_probe(void) {
    for (unsigned e=0;e<2;e++)
        for (unsigned i=0;i<32;i++) put(slot(e)+4*i,sentinel(e,i));
    for (unsigned i=0;i<32;i++) put(CPUCP+4*i,0xc0c00000u+i);
    slots_unchanged(0);
#if CASE == 1
    put(tx(0)+0x104,BELL); // Unadmitted endpoint must reject the transport.
    stop(0xbad00000u);
#endif
    admission();
#if CASE >= 2 && CASE <= 6
    put(slot(0)+4,0); put(slot(0)+16,0); put(slot(0)+20,4); put(slot(0)+24,0x4000);
#if CASE == 2
    put(tx(0)+0x104,0); // Invalid doorbell.
#elif CASE == 3
    check(begin(0,0x10,0,0,0,0,0)==0);
    // A pending completion owns the RX channel, even if the guest reclaims FREE.
    put(slot(0)+4,0); put(tx(0)+0x104,BELL);
#elif CASE == 4
    put(slot(0)+20,108); put(tx(0)+0x104,BELL);
#elif CASE == 5
    put(slot(0)+24,0x10004000); put(tx(0)+0x104,BELL);
#elif CASE == 6
    put(slot(0)+4,1); put(tx(0)+0x104,BELL); // Slot not owned by the service.
#else
#error Unsupported CASE
#endif
    stop(0xbad00000u);
#else
    // Absolute INTID66 is word two, bit two. Keep architectural IRQs masked.
    put(GIC,1); put(GIC+0x108,4); put(RX+0x4c00,0); barrier(); no_irq();
    check(begin(0,0x10,0,0,0,0,0)==0);
    check(begin(1,0x13,0,0,0,0,0)==0);
    check(get(RX+0x4400)==3 && get(RX+0x4404)==0);
    check(get(RX+0x10c)==BELL && get(RX+0x104)==BELL);
    no_irq();
    put(RX+0x4c00,2); barrier(); pending(1);
    clear(1); check(get(RX+0x4400)==2); pending(1);
    clear(1); check(get(RX+0x4400)==2); pending(1);
    put(RX+0x4c00,0); barrier(); no_irq();
    check(get(RX+0x4400)==2); // Masking does not acknowledge completion.
    put(RX+0x4c00,2); barrier(); pending(1);
    clear(2); check(get(RX+0x4400)==0); no_irq();
    check(get(RX+0x104)==BELL && get(RX+0x10c)==BELL);
    put(RX+0x4c00,1); barrier();
    check(begin(0,0x10,0,0,0,0,0)==0);
    check(begin(1,0x10,0,0,0,0,0)==0);
    pending(1); clear(2); check(get(RX+0x4400)==1); pending(1);
    clear(1); check(get(RX+0x4400)==0); no_irq();
    put(RX+0x4c00,0);
    services(0); services(1);
    cpucp_unchanged();
    stop(0x600d0000u);
#endif
}
