/*
 * sign.hpp
 *
 */

#ifndef ECDSA_SIGN_HPP_
#define ECDSA_SIGN_HPP_

//#include "CurveElement.h"
#include "P256Element.h"
#include "Tools/Bundle.h"

#include "preprocessing.hpp"

class EcSignature
{
public:
    P256Element R;
    P256Element::Scalar s;
};

/*
inline
CurveElement::Scalar hash_to_scalar(const unsigned char* message, size_t length, CurveElement::Scalar rx, CurveElement pk)
{
    crypto_hash_sha512_state state;
    crypto_hash_sha512_init(&state);
    crypto_hash_sha512_update(&state, (unsigned char*) rx.get_ptr(),
            crypto_core_ristretto255_SCALARBYTES);
    crypto_hash_sha512_update(&state, pk.get(), crypto_core_ristretto255_BYTES);
    crypto_hash_sha512_update(&state, message, length);
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512_final(&state, hash);
    auto& tmp = bigint::tmp;
    mpz_import(tmp.get_mpz_t(), crypto_hash_sha512_BYTES, -1, 1, 0, 0, hash);
    return tmp;
}
*/

inline P256Element::Scalar hash_to_scalar(const unsigned char* message, size_t length)
{
    P256Element::Scalar res;
    assert(res.size() == crypto_hash_sha256_BYTES);
    crypto_hash_sha256((unsigned char*) res.get_ptr(), message, length);
    res.zero_overhang();
    return res;
}

template<template<class U> class T>
EcSignature sign(const unsigned char* message, size_t length,
        EcTuple<T> tuple,
        typename T<P256Element::Scalar>::MAC_Check& MC, Player& P,
        P256Element pk,
        T<P256Element::Scalar> sk = {},
        SubProcessor<T<P256Element::Scalar>>* proc = 0)
{
    (void) pk;
    Timer timer;
    timer.start();
    size_t start = P.sent;
    auto stats = P.comm_stats;
    EcSignature signature;
    signature.R = tuple.R;
    T<P256Element::Scalar> prod = tuple.b;
    if (proc)
    {
        auto& protocol = proc->protocol;
        protocol.init_mul(proc);
        protocol.prepare_mul(sk, tuple.a);
        protocol.exchange();
        prod = protocol.finalize_mul();
    }
    auto rx = tuple.R.x();
    signature.s = MC.POpen(
            tuple.a * hash_to_scalar(message, length) + prod * rx, P);
    cout << "Minimal signing took " << timer.elapsed() * 1e3 << " ms and sending "
            << (P.sent - start) << " bytes" << endl;
    auto diff = (P.comm_stats - stats);
    diff.print();
    return signature;
}

inline
EcSignature sign(const unsigned char* message, size_t length, P256Element::Scalar sk)
{
    EcSignature signature;
    auto k = SeededPRNG().get<P256Element::Scalar>();
    auto inv_k = k;
    inv_k.invert();
    signature.R = k;
    auto rx = signature.R.x();
    signature.s = inv_k * (hash_to_scalar(message, length) + rx * sk);
    return signature;
}

inline
void check(EcSignature signature, const unsigned char* message, size_t length,
        P256Element pk)
{
    Timer timer;
    timer.start();
    signature.s.check();
    signature.R.check();
    P256Element::Scalar w;
    w.invert(signature.s);
    auto u1 = hash_to_scalar(message, length) * w;
    auto u2 = signature.R.x() * w;
    assert(P256Element(u1) + pk * u2 == signature.R);
    cout << "Offline checking took " << timer.elapsed() * 1e3 << " ms" << endl;
}

template<template<class U> class T>
void sign_benchmark(vector<EcTuple<T>>& tuples, T<P256Element::Scalar> sk,
        typename T<P256Element::Scalar>::MAC_Check& MCp, Player& P,
        SubProcessor<T<P256Element::Scalar>>* proc = 0)
{
    unsigned char message[1024];
    GlobalPRNG(P).get_octets(message, 1024);
    typename T<P256Element>::MAC_Check MCc(MCp.get_alphai());

    // synchronize
    Bundle<octetStream> bundle(P);
    P.Broadcast_Receive(bundle, true);
    Timer timer;
    timer.start();
    P256Element pk = MCc.POpen(sk, P);
    MCc.Check(P);
    cout << "Public key generation took " << timer.elapsed() * 1e3 << " ms" << endl;
    P.comm_stats.print();

    for (size_t i = 0; i < min(10lu, tuples.size()); i++)
    {
        check(sign(message, 1 << i, tuples[i], MCp, P, pk, sk, proc), message,
                1 << i, pk);
        Timer timer;
        timer.start();
        auto& check_player = MCp.get_check_player(P);
        auto stats = check_player.comm_stats;
        auto start = check_player.sent;
        MCp.Check(P);
        cout << "Online checking took " << timer.elapsed() * 1e3 << " ms and sending "
            << (check_player.sent - start) << " bytes" << endl;
        auto diff = (check_player.comm_stats - stats);
        diff.print();
    }
}

#endif /* ECDSA_SIGN_HPP_ */
