#include "include/PVWToBFVSeal.h"
#include "include/SealUtils.h"
#include "include/retrieval.h"
#include "include/client.h"
#include "include/LoadAndSaveUtils.h"
#include <NTL/BasicThreadPool.h>
#include <NTL/ZZ.h>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <thread>

using namespace seal;

struct BenchmarkOptions {
    string scheme;
    int threads = 1;
    int requestedN = 0;
    size_t kbar = 0;
    size_t polyModulusDegree = 0;
    int recipientBenchReps = 3;
};

struct BenchmarkResult {
    string scheme;
    int threads = 0;
    int requestedN = 0;
    size_t kbar = 0;
    double setupTimeMs = 0;
    double sendTimeMs = 0;
    double serverTimeMs = 0;
    double responseSerializationMs = 0;
    double recipientTimeMs = 0;
    double recipientMinMs = 0;
    double recipientMaxMs = 0;
    int recipientBenchReps = 0;
    double ciphertextLoadMs = 0;
    double decryptTimeMs = 0;
    double decodeTimeMs = 0;
    double totalTimeMs = 0;
    size_t digestSizeBytes = 0;
    size_t ciphertext0Bytes = 0;
    size_t ciphertext1Bytes = 0;
    size_t detectionKeySizeBytes = 0;
    bool correct = false;
    uint64_t senderSignalingGenerationNs = 0;
    uint64_t senderSerializationNs = 0;
    bool clueCorrect = true;
    size_t observedResultCount = 0;
};

uint64_t clientSenderSignalingGenerationNs = 0;
uint64_t clientSenderSerializationNs = 0;
bool clientClueCorrect = true;

int paddedTransactionCount(int requestedN, int threads, size_t degree) {
    int batch = int(degree) * threads;
    return ((requestedN + batch - 1) / batch) * batch;
}

vector<vector<uint64_t>> preparinngTransactionsFormal(PVWpk& pk, 
                                                    int numOfTransactions, int pertinentMsgNum, const PVWParam& params, bool formultitest = false,
                                                    int pertinentSelectionUpperBound = -1, const PVWsk* intendedSk = nullptr){
    srand (time(NULL));

    vector<int> msgs(numOfTransactions);
    vector<vector<uint64_t>> ret;
    vector<int> zeros(params.ell, 0);
    int selectionBound = pertinentSelectionUpperBound > 0 ? pertinentSelectionUpperBound : numOfTransactions;

    for(int i = 0; i < pertinentMsgNum;){
        auto temp = rand() % selectionBound;
        while(msgs[temp]){
            temp = rand() % selectionBound;
        }
        msgs[temp] = 1;
        i++;
    }

    cout << "Expected Message Indices: ";

    for(int i = 0; i < numOfTransactions; i++){
        PVWCiphertext tempclue;
        auto signaling_start = chrono::steady_clock::now();
        if(msgs[i]){
            cout << i << " ";
            PVWEncPK(tempclue, zeros, pk, params);
            clientSenderSignalingGenerationNs += uint64_t(chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - signaling_start).count());
            if(intendedSk){
                vector<int> decoded;
                PVWDec(decoded, tempclue, *intendedSk, params);
                clientClueCorrect = clientClueCorrect && decoded == zeros;
            }
            ret.push_back(loadDataSingle(i));
            expectedIndices.push_back(uint64_t(i));
        }
        else
        {
            auto sk2 = PVWGenerateSecretKey(params);
            PVWEncSK(tempclue, zeros, sk2, params);
            clientSenderSignalingGenerationNs += uint64_t(chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - signaling_start).count());
        }

        auto serialization_start = chrono::steady_clock::now();
        stringstream serialized_clue;
        for(size_t j = 0; j < tempclue.a.GetLength(); j++)
            serialized_clue << tempclue.a[j].ConvertToInt() << "\n";
        for(size_t j = 0; j < tempclue.b.GetLength(); j++)
            serialized_clue << tempclue.b[j].ConvertToInt() << "\n";
        clientSenderSerializationNs += uint64_t(chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - serialization_start).count());
        saveClues(tempclue, i);
    }
    cout << endl;
    return ret;
}

// Phase 1, obtaining PV's
Ciphertext serverOperations1obtainPackedSIC(vector<PVWCiphertext>& SICPVW, vector<Ciphertext> switchingKey, const RelinKeys& relin_keys,
                            const GaloisKeys& gal_keys, const size_t& degree, const SEALContext& context, const PVWParam& params, const int numOfTransactions){
    Evaluator evaluator(context);
    
    vector<Ciphertext> packedSIC(params.ell);
    cerr << "[BENCH_STAGE] OMR phase1_compute_b_plus_as_start msgs=" << numOfTransactions << endl;
    computeBplusASPVWOptimized(packedSIC, SICPVW, switchingKey, gal_keys, context, params);
    cerr << "[BENCH_STAGE] OMR phase1_compute_b_plus_as_done" << endl;

    int rangeToCheck = 850; // range check is from [-rangeToCheck, rangeToCheck-1]
    cerr << "[BENCH_STAGE] OMR phase1_range_check_start range=" << rangeToCheck << endl;
    newRangeCheckPVW(packedSIC, rangeToCheck, relin_keys, degree, context, params);
    cerr << "[BENCH_STAGE] OMR phase1_range_check_done" << endl;

    return packedSIC[0];
}

// Phase 2, retrieving
void serverOperations2therest(Ciphertext& lhs, vector<vector<int>>& bipartite_map, Ciphertext& rhs,
                        Ciphertext& packedSIC, const vector<vector<uint64_t>>& payload, const RelinKeys& relin_keys, const GaloisKeys& gal_keys,
                        const size_t& degree, const SEALContext& context, const SEALContext& context2, const PVWParam& params, const int numOfTransactions, 
                        int& counter, const int payloadSize = 306){

    Evaluator evaluator(context);
    int step = 32; // simply to save memory so process 32 msgs at a time
    
    for(int i = counter; i < counter+numOfTransactions; i += step){
        vector<Ciphertext> expandedSIC;
        int currentStep = min(step, counter + numOfTransactions - i);
        // step 1. expand PV
        expandSIC(expandedSIC, packedSIC, gal_keys, int(degree), context, context2, currentStep, i-counter);

        // transform to ntt form for better efficiency especially for the last two steps
        size_t transparentExpanded = 0;
        for(size_t j = 0; j < expandedSIC.size(); j++){
            if(expandedSIC[j].is_transparent())
                transparentExpanded++;
            if(!expandedSIC[j].is_ntt_form())
                evaluator.transform_to_ntt_inplace(expandedSIC[j]);
        }
        cerr << "[BENCH_STAGE] OMR phase2_3_expand_done offset=" << (i-counter)
             << " current_step=" << currentStep
             << " transparent=" << transparentExpanded << endl;

        // step 2. deterministic retrieval
        cerr << "[BENCH_STAGE] OMR deterministic_index_start offset=" << (i-counter) << endl;
        deterministicIndexRetrieval(lhs, expandedSIC, context, degree, i);
        cerr << "[BENCH_STAGE] OMR deterministic_index_done offset=" << (i-counter) << endl;

        // step 3-4. multiply weights and pack them
        // The following two steps are for streaming updates
        vector<vector<Ciphertext>> payloadUnpacked;
        cerr << "[BENCH_STAGE] OMR payload_retrieval_start offset=" << (i-counter) << endl;
        payloadRetrievalOptimizedwithWeights(payloadUnpacked, payload, bipartite_map_glb, weights_glb,
                                             expandedSIC, context, degree, i, i - counter, payloadSize);
        cerr << "[BENCH_STAGE] OMR payload_retrieval_done offset=" << (i-counter) << endl;
        // Note that if number of repeatitions is already set, this is the only step needed for streaming updates
        payloadPackingOptimized(rhs, payloadUnpacked, bipartite_map_glb, degree, context, gal_keys, i);   
    }
    if(lhs.is_ntt_form())
        evaluator.transform_from_ntt_inplace(lhs);
    if(rhs.is_ntt_form())
        evaluator.transform_from_ntt_inplace(rhs);

    counter += numOfTransactions;
}

// Phase 2, retrieving for OMR3
void serverOperations3therest(vector<vector<Ciphertext>>& lhs, vector<Ciphertext>& lhsCounter, vector<vector<int>>& bipartite_map, Ciphertext& rhs,
                        Ciphertext& packedSIC, const vector<vector<uint64_t>>& payload, const RelinKeys& relin_keys, const GaloisKeys& gal_keys, const PublicKey& public_key,
                        const size_t& degree, const SEALContext& context, const SEALContext& context2, const PVWParam& params, const int numOfTransactions, 
                        int& counter, const int payloadSize = 306){

    Evaluator evaluator(context);

    int step = 32;
    for(int i = counter; i < counter+numOfTransactions; i += step){
        // step 1. expand PV
        vector<Ciphertext> expandedSIC;
        expandSIC(expandedSIC, packedSIC, gal_keys, int(degree), context, context2, step, i-counter);
        // transform to ntt form for better efficiency for all of the following steps
        for(size_t j = 0; j < expandedSIC.size(); j++)
            if(!expandedSIC[j].is_ntt_form())
                evaluator.transform_to_ntt_inplace(expandedSIC[j]);
        
        // step 2. randomized retrieval
        randomizedIndexRetrieval(lhs, lhsCounter, expandedSIC, context2, public_key, i, degree, C_glb);
    
        // step 3-4. multiply weights and pack them
        // The following two steps are for streaming updates
        vector<vector<Ciphertext>> payloadUnpacked;
        payloadRetrievalOptimizedwithWeights(payloadUnpacked, payload, bipartite_map_glb, weights_glb, expandedSIC, context, degree, i, i-counter);
        // Note that if number of repeatitions is already set, this is the only step needed for streaming updates
        payloadPackingOptimized(rhs, payloadUnpacked, bipartite_map_glb, degree, context, gal_keys, i);
    }
    for(size_t i = 0; i < lhs.size(); i++){
            evaluator.transform_from_ntt_inplace(lhs[i][0]);
            evaluator.transform_from_ntt_inplace(lhs[i][1]);
            evaluator.transform_from_ntt_inplace(lhsCounter[i]);
    }
    if(rhs.is_ntt_form())
        evaluator.transform_from_ntt_inplace(rhs);
    
    counter += numOfTransactions;
}

vector<vector<long>> receiverDecoding(Ciphertext& lhsEnc, vector<vector<int>>& bipartite_map, Ciphertext& rhsEnc,
                        const size_t& degree, const SecretKey& secret_key, const SEALContext& context, const int numOfTransactions, int seed = 3,
                        const int payloadUpperBound = 306, const int payloadSize = 306, bool printIndices = true,
                        int64_t* decryptNs = nullptr){

    // 1. find pertinent indices
    map<int, int> pertinentIndices;
    decodeIndices(pertinentIndices, lhsEnc, numOfTransactions, degree, secret_key, context, decryptNs);
    if(printIndices){
        for (map<int, int>::iterator it = pertinentIndices.begin(); it != pertinentIndices.end(); it++)
        {
            std::cout << it->first << " ";  // print out all the indices found
        }
        cout << std::endl;
    }

    // 2. forming rhs
    vector<vector<int>> rhs;
    formRhs(rhs, rhsEnc, secret_key, degree, context, OMRtwoM, 306, decryptNs);

    // 3. forming lhs
    vector<vector<int>> lhs;
    formLhsWeights(lhs, pertinentIndices, bipartite_map_glb, weights_glb, 0, OMRtwoM);

    // 4. solving equation
    auto newrhs = equationSolving(lhs, rhs, payloadSize);

    return newrhs;
}

vector<vector<long>> receiverDecodingOMR3(vector<vector<Ciphertext>>& lhsEnc, vector<Ciphertext>& lhsCounter, vector<vector<int>>& bipartite_map, Ciphertext& rhsEnc,
                        const size_t& degree, const SecretKey& secret_key, const SEALContext& context, const int numOfTransactions, int seed = 3,
                        const int payloadUpperBound = 306, const int payloadSize = 306){
    // 1. find pertinent indices
    map<int, int> pertinentIndices;
    decodeIndicesRandom(pertinentIndices, lhsEnc, lhsCounter, degree, secret_key, context);
    for (map<int, int>::iterator it = pertinentIndices.begin(); it != pertinentIndices.end(); it++)
    {
        std::cout << it->first << " ";    // print out all the indices found
    }
    cout << std::endl;

    // 2. forming rhs
    vector<vector<int>> rhs;
    formRhs(rhs, rhsEnc, secret_key, degree, context, OMRthreeM);

    // 3. forming lhs
    vector<vector<int>> lhs;
    formLhsWeights(lhs, pertinentIndices, bipartite_map_glb, weights_glb, 0, OMRthreeM);

    // 4. solving equation
    auto newrhs = equationSolving(lhs, rhs, payloadSize);

    return newrhs;
}

// to check whether the result is as expected
bool checkRes(vector<vector<uint64_t>> expected, vector<vector<long>> res){
    for(size_t i = 0; i < expected.size(); i++){
        bool flag = false;
        for(size_t j = 0; j < res.size(); j++){
            if(expected[i][0] == uint64_t(res[j][0])){
                if(expected[i].size() != res[j].size())
                {
                    cerr << "expected and res length not the same" << endl;
                    return false;
                }
                for(size_t k = 1; k < res[j].size(); k++){
                    if(expected[i][k] != uint64_t(res[j][k]))
                        break;
                    if(k == res[j].size() - 1){
                        flag = true;
                    }
                }
            }
        }
        if(!flag)
            return false;
    }
    return true;
}

// check OMD detection key size
// We are:
//      1. packing PVW sk into ell ciphertexts
//      2. using seed mode in SEAL
void OMDlevelspecificDetectKeySize(){
    auto params = PVWParam(450, 65537, 1.3, 16000, 4); 
    auto sk = PVWGenerateSecretKey(params);
    cout << "Finishing generating sk for PVW cts\n";
    EncryptionParameters parms(scheme_type::bfv);
    size_t poly_modulus_degree = poly_modulus_degree_glb;
    parms.set_poly_modulus_degree(poly_modulus_degree);
    auto coeff_modulus = CoeffModulus::Create(poly_modulus_degree, { 28, 
                                                                            39, 60, 60, 60, 
                                                                            60, 60, 60, 60, 60, 60,
                                                                            32, 30, 60 });
    parms.set_coeff_modulus(coeff_modulus);
    parms.set_plain_modulus(65537);

	prng_seed_type seed;
    for (auto &i : seed)
    {
        i = random_uint64();
    }
    auto rng = make_shared<Blake2xbPRNGFactory>(Blake2xbPRNGFactory(seed));
    parms.set_random_generator(rng);

    SEALContext context(parms, true, sec_level_type::none);
    print_parameters(context); 
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    PublicKey public_key;
    keygen.create_public_key(public_key);
    RelinKeys relin_keys;
    Encryptor encryptor(context, public_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);
    BatchEncoder batch_encoder(context);
    GaloisKeys gal_keys;

    seal::Serializable<PublicKey> pk = keygen.create_public_key();
	seal::Serializable<RelinKeys> rlk = keygen.create_relin_keys();
	stringstream streamPK, streamRLK, streamRTK;
    auto reskeysize = pk.save(streamPK);
	reskeysize += rlk.save(streamRLK);
	reskeysize += keygen.create_galois_keys(vector<int>({1})).save(streamRTK);

    public_key.load(context, streamPK);
    relin_keys.load(context, streamRLK);
    gal_keys.load(context, streamRTK); 
	vector<seal::Serializable<Ciphertext>>  switchingKeypacked = genSwitchingKeyPVWPacked(context, poly_modulus_degree, public_key, secret_key, sk, params);
	stringstream data_stream;
    for(size_t i = 0; i < switchingKeypacked.size(); i++){
        reskeysize += switchingKeypacked[i].save(data_stream);
    }
    cout << "Detection Key Size: " << reskeysize << " bytes" << endl;
}

// check OMR detection key size
// We are:
//      1. packing PVW sk into ell ciphertexts
//      2. use level-specific rot keys
//      3. using seed mode in SEAL
void levelspecificDetectKeySize(){
    auto params = PVWParam(450, 65537, 1.3, 16000, 4); 
    auto sk = PVWGenerateSecretKey(params);
    cout << "Finishing generating sk for PVW cts\n";

    EncryptionParameters parms(scheme_type::bfv);
    size_t poly_modulus_degree = poly_modulus_degree_glb;
    auto degree = poly_modulus_degree;
    parms.set_poly_modulus_degree(poly_modulus_degree);
    auto coeff_modulus = CoeffModulus::Create(poly_modulus_degree, { 28, 
                                                                            39, 60, 60, 60, 60, 
                                                                            60, 60, 60, 60, 60, 60,
                                                                            32, 30, 60 });
    parms.set_coeff_modulus(coeff_modulus);
    parms.set_plain_modulus(65537);


	prng_seed_type seed;
    for (auto &i : seed)
    {
        i = random_uint64();
    }
    auto rng = make_shared<Blake2xbPRNGFactory>(Blake2xbPRNGFactory(seed));
    parms.set_random_generator(rng);

    SEALContext context(parms, true, sec_level_type::none);
    print_parameters(context); 
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    PublicKey public_key;
    keygen.create_public_key(public_key);
    RelinKeys relin_keys;
    Encryptor encryptor(context, public_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);
    BatchEncoder batch_encoder(context);
    GaloisKeys gal_keys;

    vector<int> steps = {0};
    for(int i = 1; i < int(poly_modulus_degree/2); i *= 2){
	    steps.push_back(i);
    }

    stringstream lvlRTK, lvlRTK2;
    /////////////////////////////////////// Level specific keys
    vector<Modulus> coeff_modulus_next = coeff_modulus;
    coeff_modulus_next.erase(coeff_modulus_next.begin() + 3, coeff_modulus_next.end()-1);
    EncryptionParameters parms_next = parms;
    parms_next.set_coeff_modulus(coeff_modulus_next);
    parms_next.set_random_generator(rng);
    SEALContext context_next = SEALContext(parms_next, true, sec_level_type::none);

    SecretKey sk_next;
    sk_next.data().resize(coeff_modulus_next.size() * degree);
    sk_next.parms_id() = context_next.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_next.size() - 1, sk_next.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_next.data().data() + degree * (coeff_modulus_next.size() - 1));
    KeyGenerator keygen_next(context_next, sk_next); 
    vector<int> steps_next = {0,1};
    auto reskeysize = keygen_next.create_galois_keys(steps_next).save(lvlRTK);
        //////////////////////////////////////
    vector<Modulus> coeff_modulus_last = coeff_modulus;
    coeff_modulus_last.erase(coeff_modulus_last.begin() + 2, coeff_modulus_last.end()-1);
    EncryptionParameters parms_last = parms;
    parms_last.set_coeff_modulus(coeff_modulus_last);
    parms_last.set_random_generator(rng);
    SEALContext context_last = SEALContext(parms_last, true, sec_level_type::none);

    SecretKey sk_last;
    sk_last.data().resize(coeff_modulus_last.size() * degree);
    sk_last.parms_id() = context_last.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_last.size() - 1, sk_last.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_last.data().data() + degree * (coeff_modulus_last.size() - 1));
    KeyGenerator keygen_last(context_last, sk_last); 
    reskeysize += keygen_last.create_galois_keys(steps).save(lvlRTK2);
    //////////////////////////////////////

    seal::Serializable<PublicKey> pk = keygen.create_public_key();
	seal::Serializable<RelinKeys> rlk = keygen.create_relin_keys();
	stringstream streamPK, streamRLK, streamRTK;
    reskeysize += pk.save(streamPK);
	reskeysize += rlk.save(streamRLK);
	reskeysize += keygen.create_galois_keys(vector<int>({1})).save(streamRTK);

    public_key.load(context, streamPK);
    relin_keys.load(context, streamRLK);
    gal_keys.load(context, streamRTK); 
	vector<seal::Serializable<Ciphertext>>  switchingKeypacked = genSwitchingKeyPVWPacked(context, poly_modulus_degree, public_key, secret_key, sk, params);
	stringstream data_stream;
    for(size_t i = 0; i < switchingKeypacked.size(); i++){
        reskeysize += switchingKeypacked[i].save(data_stream);
    }
    cout << "Detection Key Size: " << reskeysize << " bytes" << endl;
}

void OMD1p(){

    size_t poly_modulus_degree = poly_modulus_degree_glb;

    int numOfTransactions = numOfTransactions_glb;
    createDatabase(numOfTransactions, 306); // one time; note that this 306 represents 612 bytes because each slot can contain 2 bytes
    cout << "Finishing createDatabase\n";

    // step 1. generate PVW sk 
    // recipient side
    auto params = PVWParam(450, 65537, 1.3, 16000, 4); 
    auto sk = PVWGenerateSecretKey(params);
    auto pk = PVWGeneratePublicKey(params, sk);
    cout << "Finishing generating sk for PVW cts\n";

    // step 2. prepare transactions
    auto expected = preparinngTransactionsFormal(pk, numOfTransactions, num_of_pertinent_msgs_glb,  params);
    cout << expected.size() << " pertinent msg: Finishing preparing messages\n";



    // step 3. generate detection key
    // recipient side
    EncryptionParameters parms(scheme_type::bfv);
    parms.set_poly_modulus_degree(poly_modulus_degree);
    auto coeff_modulus = CoeffModulus::Create(poly_modulus_degree, { 28, 
                                                                            39, 60, 60, 60, 
                                                                            60, 60, 60, 60, 60, 60,
                                                                            32, 30, 60 });
    parms.set_coeff_modulus(coeff_modulus);
    parms.set_plain_modulus(65537);


	prng_seed_type seed;
    for (auto &i : seed)
    {
        i = random_uint64();
    }
    auto rng = make_shared<Blake2xbPRNGFactory>(Blake2xbPRNGFactory(seed));
    parms.set_random_generator(rng);

    SEALContext context(parms, true, sec_level_type::none);
    print_parameters(context); 
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    PublicKey public_key;
    keygen.create_public_key(public_key);
    RelinKeys relin_keys;
    keygen.create_relin_keys(relin_keys);
    Encryptor encryptor(context, public_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);
    BatchEncoder batch_encoder(context);


    vector<Ciphertext> switchingKey;
    Ciphertext packedSIC;
    switchingKey.resize(params.ell);
    // Generated BFV ciphertexts encrypting PVW secret keys
    genSwitchingKeyPVWPacked(switchingKey, context, poly_modulus_degree, public_key, secret_key, sk, params);
    
    vector<vector<PVWCiphertext>> SICPVW_multicore(numcores);
    vector<vector<vector<uint64_t>>> payload_multicore(numcores);
    vector<int> counter(numcores);

    GaloisKeys gal_keys;
    vector<int> stepsfirst = {1}; 
    // only one rot key is needed for full level
    keygen.create_galois_keys(stepsfirst, gal_keys);

    cout << "Finishing generating detection keys\n";

    vector<vector<Ciphertext>> packedSICfromPhase1(numcores,vector<Ciphertext>(numOfTransactions/numcores/poly_modulus_degree)); // Assume numOfTransactions/numcores/poly_modulus_degree is integer, pad if needed

    NTL::SetNumThreads(numcores);
    SecretKey secret_key_blank;

    chrono::high_resolution_clock::time_point time_start, time_end;
    chrono::microseconds time_diff;
    time_start = chrono::high_resolution_clock::now();

    MemoryPoolHandle my_pool = MemoryPoolHandle::New();
    auto old_prof = MemoryManager::SwitchProfile(std::make_unique<MMProfFixed>(std::move(my_pool)));
    NTL_EXEC_RANGE(numcores, first, last);
    for(int i = first; i < last; i++){
        counter[i] = numOfTransactions/numcores*i;
        
        size_t j = 0;
        while(j < numOfTransactions/numcores/poly_modulus_degree){
            cout << "OMD, Batch " << j << endl;
            loadClues(SICPVW_multicore[i], counter[i], counter[i]+poly_modulus_degree, params);
            packedSICfromPhase1[i][j] = serverOperations1obtainPackedSIC(SICPVW_multicore[i], switchingKey, relin_keys, gal_keys,
                                                            poly_modulus_degree, context, params, poly_modulus_degree);
            j++;
            counter[i] += poly_modulus_degree;
            SICPVW_multicore[i].clear();
        }
        
    }
    NTL_EXEC_RANGE_END;
    MemoryManager::SwitchProfile(std::move(old_prof));

    int determinCounter = 0;
    Ciphertext res;
    for(size_t i = 0; i < packedSICfromPhase1.size(); i++){
        for(size_t j = 0; j < packedSICfromPhase1[i].size(); j++){
            Plaintext plain_matrix;
            vector<uint64_t> pod_matrix(poly_modulus_degree, 1 << determinCounter); 
            batch_encoder.encode(pod_matrix, plain_matrix);
            if((i == 0) && (j == 0)){
                evaluator.multiply_plain(packedSICfromPhase1[i][j], plain_matrix, res);
            } else {
                evaluator.multiply_plain_inplace(packedSICfromPhase1[i][j], plain_matrix);
                evaluator.add_inplace(res, packedSICfromPhase1[i][j]);
            }
            determinCounter++;
        }
    }

    while(context.last_parms_id() != res.parms_id()){
            evaluator.mod_switch_to_next_inplace(res);
        }

    time_end = chrono::high_resolution_clock::now();
    time_diff = chrono::duration_cast<chrono::microseconds>(time_end - time_start);
    cout << "\nDetector runnimg time: " << time_diff.count() << "us." << "\n";

    // step 5. receiver decoding
    time_start = chrono::high_resolution_clock::now();
    auto realres = decodeIndicesOMD(res, numOfTransactions, poly_modulus_degree, secret_key, context);
    time_end = chrono::high_resolution_clock::now();
    time_diff = chrono::duration_cast<chrono::microseconds>(time_end - time_start);
    cout << "\nRecipient runnimg time: " << time_diff.count() << "us." << "\n";

    bool allflags = true;
    for(size_t i = 0; i < expectedIndices.size(); i++){
        bool flag = false;
        for(size_t j = 0; j < realres.size(); j++){
            if(expectedIndices[i] == realres[j])
            {
                flag = true;
                break;
            }
        }
        if(!flag){
            cout << expectedIndices[i] <<" not found" << endl;
            allflags = false;
        }
    }

    if(allflags)
        cout << "Result is correct!" << endl;
    else
        cout << "Overflow" << endl;
    
    for(size_t i = 0; i < res.size(); i++){

    }
    
}

void OMR2(const BenchmarkOptions* benchOptions = nullptr, BenchmarkResult* benchResult = nullptr){

    size_t poly_modulus_degree = poly_modulus_degree_glb;
    double setupTimeMs = 0;
    double sendTimeMs = 0;
    clientSenderSignalingGenerationNs = 0;
    clientSenderSerializationNs = 0;
    clientClueCorrect = true;
    auto setup_start = chrono::high_resolution_clock::now();

    int logicalNumOfTransactions = benchOptions ? benchOptions->requestedN : numOfTransactions_glb;
    int numOfTransactions = benchOptions ? int(poly_modulus_degree) : logicalNumOfTransactions;
    if(benchOptions){
        experimental::filesystem::create_directories("../data/payloads");
        experimental::filesystem::create_directories("../data/clues");
    }
    // Pilot mode carries one opaque uint64 location handle (four 16-bit slots).
    createDatabase(numOfTransactions, benchOptions ? 4 : 306);
    cout << "Finishing createDatabase\n";

    // step 1. generate PVW sk 
    // recipient side
    auto params = PVWParam(450, 65537, 1.3, 16000, 4); 
    auto sk = PVWGenerateSecretKey(params);
    auto pk = PVWGeneratePublicKey(params, sk);
    cout << "Finishing generating sk for PVW cts\n";

    // step 2. prepare transactions
    auto setup_pause = chrono::high_resolution_clock::now();
    setupTimeMs += double(chrono::duration_cast<chrono::microseconds>(setup_pause - setup_start).count()) / 1000.0;
    auto send_start = chrono::high_resolution_clock::now();
    auto expected = preparinngTransactionsFormal(pk, numOfTransactions, num_of_pertinent_msgs_glb,  params, false, logicalNumOfTransactions, &sk);
    auto send_end = chrono::high_resolution_clock::now();
    sendTimeMs = double(chrono::duration_cast<chrono::microseconds>(send_end - send_start).count()) / 1000.0;
    setup_start = chrono::high_resolution_clock::now();
    cout << expected.size() << " pertinent msg: Finishing preparing messages\n";

    // step 3. generate detection key
    // recipient side
    EncryptionParameters parms(scheme_type::bfv);
    auto degree = poly_modulus_degree;
    parms.set_poly_modulus_degree(poly_modulus_degree);
    auto coeff_modulus = CoeffModulus::Create(poly_modulus_degree, { 28, 
                                                                            39, 60, 60, 60, 60, 
                                                                            60, 60, 60, 60, 60, 60,
                                                                            32, 30, 60 });
    parms.set_coeff_modulus(coeff_modulus);
    parms.set_plain_modulus(65537);


	prng_seed_type seed;
    for (auto &i : seed)
    {
        i = random_uint64();
    }
    auto rng = make_shared<Blake2xbPRNGFactory>(Blake2xbPRNGFactory(seed));
    parms.set_random_generator(rng);

    SEALContext context(parms, true, sec_level_type::none);
    print_parameters(context); 
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    PublicKey public_key;
    keygen.create_public_key(public_key);
    RelinKeys relin_keys;
    keygen.create_relin_keys(relin_keys);
    Encryptor encryptor(context, public_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);
    BatchEncoder batch_encoder(context);
    const size_t slotCount = batch_encoder.slot_count();
    if (benchOptions) {
        numOfTransactions = int(slotCount);
        cout << "[PILOT_PADDING] N_logical=" << logicalNumOfTransactions
             << " N_physical=" << numOfTransactions
             << " poly_modulus_degree=" << poly_modulus_degree
             << " slot_count=" << slotCount
             << " target_count=" << num_of_pertinent_msgs_glb << endl;
    }


    vector<Ciphertext> switchingKey;
    Ciphertext packedSIC;
    switchingKey.resize(params.ell);
    genSwitchingKeyPVWPacked(switchingKey, context, poly_modulus_degree, public_key, secret_key, sk, params);
    
    vector<vector<PVWCiphertext>> SICPVW_multicore(numcores);
    vector<vector<vector<uint64_t>>> payload_multicore(numcores);
    vector<int> counter(numcores);

    GaloisKeys gal_keys;
    vector<int> stepsfirst = {1};
    // only one rot key is needed for full level
    keygen.create_galois_keys(stepsfirst, gal_keys);
    size_t detectionKeySize = 0;
    {
        stringstream streamPK, streamRLK, streamRTK;
        detectionKeySize += public_key.save(streamPK);
        detectionKeySize += relin_keys.save(streamRLK);
        detectionKeySize += gal_keys.save(streamRTK);
    }

    /////////////////////////////////////////////////////////////// Rot Key gen
    vector<int> steps = {0};
    for(int i = 1; i < int(poly_modulus_degree/2); i *= 2){
	    steps.push_back(i);
    }

    cout << "Finishing generating detection keys\n";

    /////////////////////////////////////// Level specific keys
    vector<Modulus> coeff_modulus_next = coeff_modulus;
    coeff_modulus_next.erase(coeff_modulus_next.begin() + 4, coeff_modulus_next.end()-1);
    EncryptionParameters parms_next = parms;
    parms_next.set_coeff_modulus(coeff_modulus_next);
    SEALContext context_next = SEALContext(parms_next, true, sec_level_type::none);

    SecretKey sk_next;
    sk_next.data().resize(coeff_modulus_next.size() * degree);
    sk_next.parms_id() = context_next.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_next.size() - 1, sk_next.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_next.data().data() + degree * (coeff_modulus_next.size() - 1));
    KeyGenerator keygen_next(context_next, sk_next); 
    vector<int> steps_next = {0,1};
    keygen_next.create_galois_keys(steps_next, gal_keys_next);
    {
        stringstream lvlRTK;
        detectionKeySize += gal_keys_next.save(lvlRTK);
    }
        //////////////////////////////////////
    vector<Modulus> coeff_modulus_last = coeff_modulus;
    coeff_modulus_last.erase(coeff_modulus_last.begin() + 2, coeff_modulus_last.end()-1);
    EncryptionParameters parms_last = parms;
    parms_last.set_coeff_modulus(coeff_modulus_last);
    SEALContext context_last = SEALContext(parms_last, true, sec_level_type::none);

    SecretKey sk_last;
    sk_last.data().resize(coeff_modulus_last.size() * degree);
    sk_last.parms_id() = context_last.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_last.size() - 1, sk_last.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_last.data().data() + degree * (coeff_modulus_last.size() - 1));
    KeyGenerator keygen_last(context_last, sk_last); 
    keygen_last.create_galois_keys(steps, gal_keys_last);
    {
        stringstream lvlRTK2;
        detectionKeySize += gal_keys_last.save(lvlRTK2);
    }
    //////////////////////////////////////

    {
        stringstream switchingKeyStream;
        for(size_t i = 0; i < switchingKey.size(); i++){
            detectionKeySize += switchingKey[i].save(switchingKeyStream);
        }
    }
    setup_pause = chrono::high_resolution_clock::now();
    setupTimeMs += double(chrono::duration_cast<chrono::microseconds>(setup_pause - setup_start).count()) / 1000.0;

    int totalBatches = (numOfTransactions + int(poly_modulus_degree) - 1) / int(poly_modulus_degree);
    vector<Ciphertext> packedSICfromPhase1(totalBatches);

    NTL::SetNumThreads(numcores);
    SecretKey secret_key_blank;

    int64_t serverComputeUs = 0;

    MemoryPoolHandle my_pool = MemoryPoolHandle::New();
    auto old_prof = MemoryManager::SwitchProfile(std::make_unique<MMProfFixed>(std::move(my_pool)));
    NTL_EXEC_RANGE(totalBatches, first, last);
    for(int batch = first; batch < last; batch++){
        int start = batch * int(poly_modulus_degree);
        int end = min(start + int(poly_modulus_degree), numOfTransactions);
        if(batch == 0)
            cout << "Phase 1, Batch " << batch << endl;
        cerr << "[BENCH_STAGE] OMR phase1_load_clues_start batch=" << batch
             << " start=" << start << " end=" << end << endl;
        vector<PVWCiphertext> SICPVW_batch;
        loadClues(SICPVW_batch, start, end, params);
        cerr << "[BENCH_STAGE] OMR phase1_load_clues_done batch=" << batch << endl;
        vector<Ciphertext> switchingKeyLocal = switchingKey;
        auto core_start = chrono::high_resolution_clock::now();
        packedSICfromPhase1[batch] = serverOperations1obtainPackedSIC(SICPVW_batch, switchingKeyLocal, relin_keys, gal_keys,
                                                        poly_modulus_degree, context, params, end - start);
        serverComputeUs += chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now() - core_start).count();
        cerr << "[BENCH_STAGE] OMR phase1_batch_done batch=" << batch << endl;
    }
    NTL_EXEC_RANGE_END;
    MemoryManager::SwitchProfile(std::move(old_prof));

    // step 4. detector operations
    vector<Ciphertext> lhs_multi(totalBatches), rhs_multi(totalBatches);
    vector<vector<vector<int>>> bipartite_map(totalBatches);

    bipartiteGraphWeightsGeneration(bipartite_map_glb, weights_glb, numOfTransactions,OMRtwoM,repeatition_glb,seed_glb);

    NTL_EXEC_RANGE(totalBatches, first, last);
    for(int batch = first; batch < last; batch++){
        MemoryPoolHandle my_pool = MemoryPoolHandle::New();
        auto old_prof = MemoryManager::SwitchProfile(std::make_unique<MMProfFixed>(std::move(my_pool)));
        int start = batch * int(poly_modulus_degree);
        int end = min(start + int(poly_modulus_degree), numOfTransactions);
        int batchCounter = start;
        if(batch == 0)
            cout << "Phase 2-3, Batch " << batch << endl;
        cerr << "[BENCH_STAGE] OMR phase2_3_load_payload_start batch=" << batch
             << " start=" << start << " end=" << end << endl;
        vector<vector<uint64_t>> payload_batch;
        loadData(payload_batch, start, end, benchOptions ? 4 : 306);
        cerr << "[BENCH_STAGE] OMR phase2_3_server_ops_start batch=" << batch << endl;
        auto core_start = chrono::high_resolution_clock::now();
        serverOperations2therest(lhs_multi[batch], bipartite_map[batch], rhs_multi[batch],
                        packedSICfromPhase1[batch], payload_batch, relin_keys, gal_keys_next,
                        poly_modulus_degree, context_next, context_last, params, end - start, batchCounter,
                        benchOptions ? 4 : 306);
        serverComputeUs += chrono::duration_cast<chrono::microseconds>(
            chrono::high_resolution_clock::now() - core_start).count();
        cerr << "[BENCH_STAGE] OMR phase2_3_batch_done batch=" << batch << endl;
        
        MemoryManager::SwitchProfile(std::move(old_prof));
    }
    NTL_EXEC_RANGE_END;

    auto aggregate_start = chrono::high_resolution_clock::now();
    for(int i = 1; i < totalBatches; i++){
        evaluator.add_inplace(lhs_multi[0], lhs_multi[i]);
        evaluator.add_inplace(rhs_multi[0], rhs_multi[i]);
    }

    while(context.last_parms_id() != lhs_multi[0].parms_id()){
            evaluator.mod_switch_to_next_inplace(rhs_multi[0]);
            evaluator.mod_switch_to_next_inplace(lhs_multi[0]);
        }
    serverComputeUs += chrono::duration_cast<chrono::microseconds>(
        chrono::high_resolution_clock::now() - aggregate_start).count();

    cout << "\nDetector runnimg time: " << serverComputeUs << "us." << "\n";
    double serverTimeMs = double(serverComputeUs) / 1000.0;

    auto serialize_start = chrono::high_resolution_clock::now();
    stringstream data_streamdg, data_streamdg2;
    rhs_multi[0].save(data_streamdg);
    lhs_multi[0].save(data_streamdg2);
    const string rhsBlob = data_streamdg.str();
    const string lhsBlob = data_streamdg2.str();
    string responseBuffer;
    auto appendLengthLE = [&](uint64_t length) {
        for (int shift = 0; shift < 64; shift += 8)
            responseBuffer.push_back(char((length >> shift) & 0xff));
    };
    appendLengthLE(rhsBlob.size()); responseBuffer.append(rhsBlob);
    appendLengthLE(lhsBlob.size()); responseBuffer.append(lhsBlob);
    size_t digestSize = responseBuffer.size();
    const size_t ciphertext0Bytes = rhsBlob.size();
    const size_t ciphertext1Bytes = lhsBlob.size();
    double responseSerializationMs = double(chrono::duration_cast<chrono::microseconds>(
        chrono::high_resolution_clock::now() - serialize_start).count()) / 1000.0;
    cout << "Digest size: " << digestSize << " bytes" << endl;

    // step 5. receiver decoding. The server response is already complete;
    // parse framing, load both native SEAL ciphertexts, decrypt, and decode.
    bipartiteGraphWeightsGeneration(bipartite_map_glb, weights_glb, numOfTransactions,OMRtwoM,repeatition_glb,seed_glb);
    auto readLengthLE = [&](size_t& offset) -> uint64_t {
        if(offset + 8 > responseBuffer.size()) throw runtime_error("truncated OMR response framing");
        uint64_t value = 0;
        for(int shift = 0; shift < 64; shift += 8)
            value |= uint64_t(uint8_t(responseBuffer[offset++])) << shift;
        return value;
    };
    const int recipientReps = benchOptions ? benchOptions->recipientBenchReps : 1;
    vector<double> recipientSamplesMs;
    vector<double> loadSamplesMs, decryptSamplesMs, decodeSamplesMs;
    vector<vector<long>> res;
    for(int rep = 0; rep < recipientReps; ++rep){
        const auto recipientStart = chrono::steady_clock::now();
        const auto loadStart = recipientStart;
        size_t offset = 0;
        const uint64_t rhsLength = readLengthLE(offset);
        if(rhsLength > responseBuffer.size() - offset) throw runtime_error("invalid OMR rhs ciphertext length");
        const string rhsSerialized = responseBuffer.substr(offset, size_t(rhsLength));
        offset += size_t(rhsLength);
        const uint64_t lhsLength = readLengthLE(offset);
        if(lhsLength > responseBuffer.size() - offset || offset + lhsLength != responseBuffer.size())
            throw runtime_error("invalid OMR lhs ciphertext length");
        const string lhsSerialized = responseBuffer.substr(offset, size_t(lhsLength));
        istringstream rhsStream(rhsSerialized, ios::in | ios::binary);
        istringstream lhsStream(lhsSerialized, ios::in | ios::binary);
        Ciphertext rhsLoaded, lhsLoaded;
        rhsLoaded.load(context, rhsStream);
        lhsLoaded.load(context, lhsStream);
        const auto loadEnd = chrono::steady_clock::now();
        int64_t decryptNs = 0;
        const auto decodeStart = loadEnd;
        auto decoded = receiverDecoding(lhsLoaded, bipartite_map[0], rhsLoaded,
                            poly_modulus_degree, secret_key, context, numOfTransactions, 3,
                            benchOptions ? 4 : 306, benchOptions ? 4 : 306, false, &decryptNs);
        const auto recipientEnd = chrono::steady_clock::now();
        const int64_t decodeTotalNs = chrono::duration_cast<chrono::nanoseconds>(
            recipientEnd - decodeStart).count();
        loadSamplesMs.push_back(double(chrono::duration_cast<chrono::nanoseconds>(
            loadEnd - loadStart).count()) / 1000000.0);
        decryptSamplesMs.push_back(double(decryptNs) / 1000000.0);
        decodeSamplesMs.push_back(double(decodeTotalNs - decryptNs) / 1000000.0);
        recipientSamplesMs.push_back(double(chrono::duration_cast<chrono::nanoseconds>(
            recipientEnd - recipientStart).count()) / 1000000.0);
        res = std::move(decoded);
    }
    vector<double> sortedRecipientSamples = recipientSamplesMs;
    sort(sortedRecipientSamples.begin(), sortedRecipientSamples.end());
    const double recipientTimeMs = sortedRecipientSamples[sortedRecipientSamples.size() / 2];
    const double recipientMinMs = sortedRecipientSamples.front();
    const double recipientMaxMs = sortedRecipientSamples.back();
    auto medianSample = [](vector<double> samples) {
        sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    const double ciphertextLoadMs = medianSample(loadSamplesMs);
    const double decryptTimeMs = medianSample(decryptSamplesMs);
    const double decodeTimeMs = medianSample(decodeSamplesMs);
    cout << "\nRecipient response consumption median: " << recipientTimeMs << "ms." << "\n";

    bool correct = checkRes(expected, res);
    if(correct)
        cout << "Result is correct!" << endl;
    else
        cout << "Overflow" << endl;

    if(benchResult){
        benchResult->scheme = "omrp1";
        benchResult->threads = numcores;
        benchResult->requestedN = logicalNumOfTransactions;
        benchResult->kbar = num_of_pertinent_msgs_glb;
        benchResult->setupTimeMs = setupTimeMs;
        benchResult->sendTimeMs = sendTimeMs;
        benchResult->serverTimeMs = serverTimeMs;
        benchResult->responseSerializationMs = responseSerializationMs;
        benchResult->recipientTimeMs = recipientTimeMs;
        benchResult->recipientMinMs = recipientMinMs;
        benchResult->recipientMaxMs = recipientMaxMs;
        benchResult->recipientBenchReps = recipientReps;
        benchResult->ciphertextLoadMs = ciphertextLoadMs;
        benchResult->decryptTimeMs = decryptTimeMs;
        benchResult->decodeTimeMs = decodeTimeMs;
        benchResult->totalTimeMs = setupTimeMs + sendTimeMs + serverTimeMs + recipientTimeMs;
        benchResult->digestSizeBytes = digestSize;
        benchResult->ciphertext0Bytes = ciphertext0Bytes;
        benchResult->ciphertext1Bytes = ciphertext1Bytes;
        benchResult->detectionKeySizeBytes = detectionKeySize;
        benchResult->correct = correct;
        benchResult->senderSignalingGenerationNs = clientSenderSignalingGenerationNs;
        benchResult->senderSerializationNs = clientSenderSerializationNs;
        benchResult->clueCorrect = clientClueCorrect;
        benchResult->observedResultCount = res.size();
    }
}

void OMR3(){

    size_t poly_modulus_degree = poly_modulus_degree_glb;

    int numOfTransactions = numOfTransactions_glb;
    createDatabase(numOfTransactions, 306); 
    cout << "Finishing createDatabase\n";

    // step 1. generate PVW sk
    // recipient side
    auto params = PVWParam(450, 65537, 1.3, 16000, 4); 
    auto sk = PVWGenerateSecretKey(params);
    auto pk = PVWGeneratePublicKey(params, sk);
    cout << "Finishing generating sk for PVW cts\n";

    // step 2. prepare transactions
    auto expected = preparinngTransactionsFormal(pk, numOfTransactions, num_of_pertinent_msgs_glb,  params);
    cout << expected.size() << " pertinent msg: Finishing preparing messages\n";



    // step 3. generate detection key
    // recipient side
    EncryptionParameters parms(scheme_type::bfv);
    auto degree = poly_modulus_degree;
    parms.set_poly_modulus_degree(poly_modulus_degree);
    auto coeff_modulus = CoeffModulus::Create(poly_modulus_degree, { 28, 
                                                                            39, 60, 60, 60, 60, 
                                                                            60, 60, 60, 60, 60, 60,
                                                                            32, 30, 60 });
    parms.set_coeff_modulus(coeff_modulus);
    parms.set_plain_modulus(65537);


	prng_seed_type seed;
    for (auto &i : seed)
    {
        i = random_uint64();
    }
    auto rng = make_shared<Blake2xbPRNGFactory>(Blake2xbPRNGFactory(seed));
    parms.set_random_generator(rng);

    SEALContext context(parms, true, sec_level_type::none);
    print_parameters(context); 
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    PublicKey public_key;
    keygen.create_public_key(public_key);
    RelinKeys relin_keys;
    keygen.create_relin_keys(relin_keys);
    Encryptor encryptor(context, public_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);
    BatchEncoder batch_encoder(context);


    vector<Ciphertext> switchingKey;
    Ciphertext packedSIC;
    switchingKey.resize(params.ell);
    genSwitchingKeyPVWPacked(switchingKey, context, poly_modulus_degree, public_key, secret_key, sk, params);
    
    vector<vector<PVWCiphertext>> SICPVW_multicore(numcores);
    vector<vector<vector<uint64_t>>> payload_multicore(numcores);
    vector<int> counter(numcores);

    GaloisKeys gal_keys;
    vector<int> stepsfirst = {1};
    keygen.create_galois_keys(stepsfirst, gal_keys);

    /////////////////////////////////////////////////////////////// Rot Key gen
    vector<int> steps = {0};
    for(int i = 1; i < int(poly_modulus_degree/2); i *= 2){
	    steps.push_back(i);
    }

    cout << "Finishing generating detection keys\n";

    /////////////////////////////////////// Level specific keys
    vector<Modulus> coeff_modulus_next = coeff_modulus;
    coeff_modulus_next.erase(coeff_modulus_next.begin() + 4, coeff_modulus_next.end()-1);
    EncryptionParameters parms_next = parms;
    parms_next.set_coeff_modulus(coeff_modulus_next);
    SEALContext context_next = SEALContext(parms_next, true, sec_level_type::none);

    SecretKey sk_next;
    sk_next.data().resize(coeff_modulus_next.size() * degree);
    sk_next.parms_id() = context_next.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_next.size() - 1, sk_next.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_next.data().data() + degree * (coeff_modulus_next.size() - 1));
    KeyGenerator keygen_next(context_next, sk_next); 
    vector<int> steps_next = {0,1};
    keygen_next.create_galois_keys(steps_next, gal_keys_next);
        //////////////////////////////////////
    vector<Modulus> coeff_modulus_last = coeff_modulus;
    coeff_modulus_last.erase(coeff_modulus_last.begin() + 2, coeff_modulus_last.end()-1);
    EncryptionParameters parms_last = parms;
    parms_last.set_coeff_modulus(coeff_modulus_last);
    SEALContext context_last = SEALContext(parms_last, true, sec_level_type::none);

    SecretKey sk_last;
    sk_last.data().resize(coeff_modulus_last.size() * degree);
    sk_last.parms_id() = context_last.key_parms_id();
    util::set_poly(secret_key.data().data(), degree, coeff_modulus_last.size() - 1, sk_last.data().data());
    util::set_poly(
        secret_key.data().data() + degree * (coeff_modulus.size() - 1), degree, 1,
        sk_last.data().data() + degree * (coeff_modulus_last.size() - 1));
    KeyGenerator keygen_last(context_last, sk_last); 
    keygen_last.create_galois_keys(steps, gal_keys_last);
    PublicKey public_key_last;
    keygen_last.create_public_key(public_key_last);
    
    //////////////////////////////////////

    vector<vector<Ciphertext>> packedSICfromPhase1(numcores,vector<Ciphertext>(numOfTransactions/numcores/poly_modulus_degree)); // Assume numOfTransactions/numcores/poly_modulus_degree is integer, pad if needed

    NTL::SetNumThreads(numcores);
    SecretKey secret_key_blank;

    chrono::high_resolution_clock::time_point time_start, time_end;
    chrono::microseconds time_diff;
    time_start = chrono::high_resolution_clock::now();

    MemoryPoolHandle my_pool = MemoryPoolHandle::New();
    auto old_prof = MemoryManager::SwitchProfile(std::make_unique<MMProfFixed>(std::move(my_pool)));
    NTL_EXEC_RANGE(numcores, first, last);
    for(int i = first; i < last; i++){
        counter[i] = numOfTransactions/numcores*i;
        
        size_t j = 0;
        while(j < numOfTransactions/numcores/poly_modulus_degree){
            if(!i)
                cout << "Phase 1, Core " << i << ", Batch " << j << endl;
            loadClues(SICPVW_multicore[i], counter[i], counter[i]+poly_modulus_degree, params);
            packedSICfromPhase1[i][j] = serverOperations1obtainPackedSIC(SICPVW_multicore[i], switchingKey, relin_keys, gal_keys,
                                                            poly_modulus_degree, context, params, poly_modulus_degree);
            j++;
            counter[i] += poly_modulus_degree;
            SICPVW_multicore[i].clear();
        }
        
    }
    NTL_EXEC_RANGE_END;
    MemoryManager::SwitchProfile(std::move(old_prof));


    // step 4. detector operations
    vector<vector<vector<Ciphertext>>> lhs_multi(numcores);
    vector<vector<Ciphertext>> lhs_multi_ctr(numcores);
    vector<Ciphertext> rhs_multi(numcores);
    vector<vector<vector<int>>> bipartite_map(numcores);

    bipartiteGraphWeightsGeneration(bipartite_map_glb, weights_glb, numOfTransactions,OMRtwoM,repeatition_glb,seed_glb);


    NTL_EXEC_RANGE(numcores, first, last);
    for(int i = first; i < last; i++){
        MemoryPoolHandle my_pool = MemoryPoolHandle::New();
        auto old_prof = MemoryManager::SwitchProfile(std::make_unique<MMProfFixed>(std::move(my_pool)));
        size_t j = 0;
        counter[i] = numOfTransactions/numcores*i;

        while(j < numOfTransactions/numcores/poly_modulus_degree){
            if(!i)
                cout << "Phase 2-3, Core " << i << ", Batch " << j << endl;
            loadData(payload_multicore[i], counter[i], counter[i]+poly_modulus_degree);
            vector<vector<Ciphertext>> templhs;
            vector<Ciphertext> templhsctr;
            Ciphertext temprhs;
            serverOperations3therest(templhs, templhsctr, bipartite_map[i], temprhs,
                            packedSICfromPhase1[i][j], payload_multicore[i], relin_keys, gal_keys_next, public_key_last,
                            poly_modulus_degree, context_next, context_last, params, poly_modulus_degree, counter[i]);
            if(j == 0){
                lhs_multi[i] = templhs;
                lhs_multi_ctr[i] = templhsctr;
                rhs_multi[i] = temprhs;
            } else {
                for(size_t q = 0; q < lhs_multi[i].size(); q++){
                    for(size_t w = 0; w < lhs_multi[i][q].size(); w++){
                        evaluator.add_inplace(lhs_multi[i][q][w], templhs[q][w]);
                    }
                }
                for(size_t q = 0; q < lhs_multi_ctr[i].size(); q++){
                    evaluator.add_inplace(lhs_multi_ctr[i][q], templhsctr[q]);
                }
                evaluator.add_inplace(rhs_multi[i], temprhs);
            }
            j++;
            payload_multicore[i].clear();
        }
        
        MemoryManager::SwitchProfile(std::move(old_prof));
    }
    NTL_EXEC_RANGE_END;

    for(int i = 1; i < numcores; i++){
        for(size_t q = 0; q < lhs_multi[i].size(); q++){
            for(size_t w = 0; w < lhs_multi[i][q].size(); w++){
                evaluator.add_inplace(lhs_multi[0][q][w], lhs_multi[i][q][w]);
            }
        }
        for(size_t q = 0; q < lhs_multi_ctr[i].size(); q++){
            evaluator.add_inplace(lhs_multi_ctr[0][q], lhs_multi_ctr[i][q]);
        }
        evaluator.add_inplace(rhs_multi[0], rhs_multi[i]);
    }

    while(context.last_parms_id() != lhs_multi[0][0][0].parms_id()){
            for(size_t q = 0; q < lhs_multi[0].size(); q++){
                for(size_t w = 0; w < lhs_multi[0][q].size(); w++){
                    evaluator.mod_switch_to_next_inplace(lhs_multi[0][q][w]);
                }
            }
            for(size_t q = 0; q < lhs_multi_ctr[0].size(); q++){
                evaluator.mod_switch_to_next_inplace(lhs_multi_ctr[0][q]);
            }
            evaluator.mod_switch_to_next_inplace(rhs_multi[0]);
        }

    time_end = chrono::high_resolution_clock::now();
    time_diff = chrono::duration_cast<chrono::microseconds>(time_end - time_start);
    cout << "\nDetector runnimg time: " << time_diff.count() << "us." << "\n";

    stringstream data_streamdg, data_streamdg2;
    auto digsize = rhs_multi[0].save(data_streamdg);
    for(size_t q = 0; q < lhs_multi[0].size(); q++){
        for(size_t w = 0; w < lhs_multi[0][q].size(); w++){
            digsize += lhs_multi[0][q][w].save(data_streamdg2);
        }
    }
    for(size_t q = 0; q < lhs_multi_ctr[0].size(); q++){
        digsize += lhs_multi_ctr[0][q].save(data_streamdg2);
    }
    cout << "Digest size: " << digsize << " bytes" << endl;

    // step 5. receiver decoding
    bipartiteGraphWeightsGeneration(bipartite_map_glb, weights_glb, numOfTransactions,OMRtwoM,repeatition_glb,seed_glb);
    time_start = chrono::high_resolution_clock::now();
    auto res = receiverDecodingOMR3(lhs_multi[0], lhs_multi_ctr[0], bipartite_map[0], rhs_multi[0],
                        poly_modulus_degree, secret_key, context, numOfTransactions);
    time_end = chrono::high_resolution_clock::now();
    time_diff = chrono::duration_cast<chrono::microseconds>(time_end - time_start);
    cout << "\nRecipient runnimg time: " << time_diff.count() << "us." << "\n";

    if(checkRes(expected, res))
        cout << "Result is correct!" << endl;
    else
        cout << "Overflow" << endl;
    
    for(size_t i = 0; i < res.size(); i++){

    }
    
}

void printBenchmarkUsage(const char* prog) {
    cerr << "Usage: " << prog << " --bench omrp1 --threads <1|2|4> --N <transactions> --kbar <bound>" << endl;
}

int parsePositiveInt(const string& value, const string& name) {
    size_t parsed = 0;
    int result = stoi(value, &parsed);
    if(parsed != value.size() || result <= 0){
        throw invalid_argument(name + " must be a positive integer");
    }
    return result;
}

bool parseBenchmarkOptions(int argc, char** argv, BenchmarkOptions& options) {
    bool hasBench = false;
    for(int i = 1; i < argc; i++){
        string arg(argv[i]);
        if(arg == "--bench"){
            if(i + 1 >= argc){
                throw invalid_argument("--bench requires a value");
            }
            hasBench = true;
            options.scheme = argv[++i];
        } else if(arg == "--threads"){
            if(i + 1 >= argc){
                throw invalid_argument("--threads requires a value");
            }
            options.threads = parsePositiveInt(argv[++i], "--threads");
        } else if(arg == "--N"){
            if(i + 1 >= argc){
                throw invalid_argument("--N requires a value");
            }
            options.requestedN = parsePositiveInt(argv[++i], "--N");
        } else if(arg == "--kbar"){
            if(i + 1 >= argc){
                throw invalid_argument("--kbar requires a value");
            }
            options.kbar = size_t(parsePositiveInt(argv[++i], "--kbar"));
        } else if(arg == "--poly-modulus-degree"){
            if(i + 1 >= argc){
                throw invalid_argument("--poly-modulus-degree requires a value");
            }
            options.polyModulusDegree = size_t(parsePositiveInt(argv[++i], "--poly-modulus-degree"));
        } else if(arg == "--recipient-bench-reps"){
            if(i + 1 >= argc){
                throw invalid_argument("--recipient-bench-reps requires a value");
            }
            options.recipientBenchReps = parsePositiveInt(argv[++i], "--recipient-bench-reps");
        } else if(arg == "--help" || arg == "-h"){
            printBenchmarkUsage(argv[0]);
            exit(0);
        } else {
            throw invalid_argument("unknown option: " + arg);
        }
    }
    return hasBench;
}

int runBenchmark(const BenchmarkOptions& options) {
    if(options.scheme != "omrp1"){
        cerr << "Unsupported benchmark scheme: " << options.scheme << endl;
        return 1;
    }
    if(options.threads != 1 && options.threads != 2 && options.threads != 4){
        cerr << "--threads must be 1, 2, or 4" << endl;
        return 1;
    }
    if(options.requestedN <= 0){
        cerr << "--N is required for benchmark mode" << endl;
        return 1;
    }
    if(options.kbar == 0){
        cerr << "--kbar is required for benchmark mode" << endl;
        return 1;
    }
    if(options.kbar > size_t(options.requestedN)){
        cerr << "--kbar must be <= --N" << endl;
        return 1;
    }

    expectedIndices.clear();
    bipartite_map_glb.clear();
    weights_glb.clear();
    numcores = options.threads;
    numOfTransactions_glb = options.requestedN;
    num_of_pertinent_msgs_glb = options.kbar;
    if(options.polyModulusDegree != 0){
        poly_modulus_degree_glb = options.polyModulusDegree;
    }

    BenchmarkResult result;
    OMR2(&options, &result);
    cout << "[OMR_CSV] scheme,threads,N,kbar,server_time_ms,recipient_time_ms,digest_size_bytes,detection_key_size_bytes" << endl;
    cout << "[OMR_CSV] "
         << result.scheme << ","
         << result.threads << ","
         << result.requestedN << ","
         << result.kbar << ","
         << result.serverTimeMs << ","
         << result.recipientTimeMs << ","
         << result.digestSizeBytes << ","
         << result.detectionKeySizeBytes << endl;
    string bottleneck = "setup";
    double best = result.setupTimeMs;
    if(result.sendTimeMs > best){
        bottleneck = "send";
        best = result.sendTimeMs;
    }
    if(result.serverTimeMs > best){
        bottleneck = "server";
        best = result.serverTimeMs;
    }
    if(result.recipientTimeMs > best){
        bottleneck = "recipient";
    }
    cout << "[BENCH_CSV] scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck" << endl;
    cout << "[BENCH_CSV] "
         << "OMR" << ","
         << result.requestedN << ","
         << result.kbar << ","
         << result.setupTimeMs << ","
         << result.sendTimeMs << ","
         << result.serverTimeMs << ","
         << result.recipientTimeMs << ","
         << result.digestSizeBytes << ","
         << (result.correct ? "completed" : "crashed") << ","
         << (result.correct ? bottleneck : "result_check_failed") << endl;
    const bool clientCorrect = result.correct && result.clueCorrect;
    cout << "[CLIENT_BENCH_JSON] {"
         << "\"scheme\":\"OMR\","
         << "\"Ns\":" << result.requestedN << ","
         << "\"Nr\":null,"
         << "\"actual_k\":" << result.kbar << ","
         << "\"observed_result_count\":" << result.observedResultCount << ","
         << "\"sender_signaling_generation_ns\":" << result.senderSignalingGenerationNs << ","
         << "\"sender_serialization_ns\":" << result.senderSerializationNs << ","
         << "\"sender_total_online_ns\":" << result.senderSignalingGenerationNs + result.senderSerializationNs << ","
         << "\"recipient_processing_ns\":" << uint64_t(result.recipientTimeMs * 1000000.0) << ","
         << "\"correctness\":" << (clientCorrect ? "true" : "false") << ","
         << "\"output_type\":\"decoded_pertinent_payloads\","
         << "\"error_class\":\"" << (clientCorrect ? "" : "clue_or_decode_mismatch") << "\","
         << "\"scheme_specific_parameters\":\"PVW_n=450;PVW_q=65537;PVW_stddev=1.3;PVW_m=16000;PVW_ell=4;kbar="
         << result.kbar << ";threads=" << result.threads << "\"}" << endl;
    cout << "[RECIPIENT_JSON] {"
         << "\"scheme\":\"OMR\",\"N\":" << result.requestedN
         << ",\"k_actual\":" << result.kbar
         << ",\"response_bytes\":" << result.digestSizeBytes
         << ",\"ciphertext0_bytes\":" << result.ciphertext0Bytes
         << ",\"ciphertext1_bytes\":" << result.ciphertext1Bytes
         << ",\"recipient_processing_ns\":" << uint64_t(result.recipientTimeMs * 1000000.0)
         << ",\"recipient_min_ns\":" << uint64_t(result.recipientMinMs * 1000000.0)
         << ",\"recipient_max_ns\":" << uint64_t(result.recipientMaxMs * 1000000.0)
         << ",\"recipient_inner_ops\":" << result.recipientBenchReps
         << ",\"correctness\":" << (result.correct ? "true" : "false")
         << ",\"consumer_operations\":\"parse_framing_ciphertext_load_decrypt_decode\"}" << endl;
    cout << "[PILOT_JSON] {"
         << "\"scheme\":\"OMR\",\"N\":" << result.requestedN
         << ",\"k_actual\":" << result.kbar
         << ",\"poly_modulus_degree\":" << poly_modulus_degree_glb
         << ",\"slot_count\":" << poly_modulus_degree_glb
         << ",\"parameter_mode\":\"reduced_parameter_pilot\""
         << ",\"security_comparable\":false"
         << ",\"admission_status\":\"unsupported\",\"admission_online_ms\":null"
         << ",\"retrieval_core_ms\":" << result.serverTimeMs
         << ",\"response_serialization_ms\":" << result.responseSerializationMs
         << ",\"retrieval_online_ms\":" << result.serverTimeMs + result.responseSerializationMs
         << ",\"server0_response_bytes\":" << result.digestSizeBytes
         << ",\"ciphertext0_bytes\":" << result.ciphertext0Bytes
         << ",\"ciphertext1_bytes\":" << result.ciphertext1Bytes
         << ",\"server1_response_bytes\":null"
         << ",\"server_to_recipient_bytes\":" << result.digestSizeBytes
         << ",\"candidate_count\":null,\"false_positive_count\":null"
         << ",\"correctness_status\":\"" << (result.correct ? "passed" : "failed") << "\""
         << ",\"measurement_quality\":\"reduced_parameter_pilot\""
         << ",\"measurement_status\":\"ok\"}" << endl;
    cout << "[OMR_SCALABILITY_JSON] {"
         << "\"N\":" << result.requestedN
         << ",\"poly_modulus_degree\":" << poly_modulus_degree_glb
         << ",\"slot_count\":" << poly_modulus_degree_glb
         << ",\"ciphertext_size\":" << result.digestSizeBytes
         << ",\"retrieval_ms\":" << result.serverTimeMs + result.responseSerializationMs
         << ",\"response_serialization_ms\":" << result.responseSerializationMs
         << ",\"load_ms\":" << result.ciphertextLoadMs
         << ",\"eval_ms\":" << result.serverTimeMs
         << ",\"decrypt_ms\":" << result.decryptTimeMs
         << ",\"decode_ms\":" << result.decodeTimeMs
         << ",\"correctness\":" << (result.correct ? "true" : "false")
         << "}" << endl;
    return 0;
}

int main(int argc, char** argv){

    if(argc > 1){
        BenchmarkOptions options;
        try {
            if(parseBenchmarkOptions(argc, argv, options)){
                return runBenchmark(options);
            }
        } catch(const exception& e) {
            cerr << e.what() << endl;
            printBenchmarkUsage(argv[0]);
            return 1;
        }
    }

    cout << "+------------------------------------+" << endl;
    cout << "| Demos                              |" << endl;
    cout << "+------------------------------------+" << endl;
    cout << "| 1. OMD1p Detection Key Size        |" << endl;
    cout << "| 2. OMR1p/OMR2p Detection Key Size  |" << endl;
    cout << "| 3. OMD1p                           |" << endl;
    cout << "| 4. OMR1p Single Thread             |" << endl;
    cout << "| 5. OMR2p Single Thread             |" << endl;
    cout << "| 6. OMR1p Two Threads               |" << endl;
    cout << "| 7. OMR2p Two Threads               |" << endl;
    cout << "| 8. OMR1p Four Threads              |" << endl;
    cout << "| 9. OMR2p Four Threads              |" << endl;
    cout << "+------------------------------------+" << endl;

    int selection = 0;
    bool valid = true;
    do
    {
        cout << endl << "> Run demos (1 ~ 9) or exit (0): ";
        if (!(cin >> selection))
        {
            valid = false;
        }
        else if (selection < 0 || selection > 9)
        {
            valid = false;
        }
        else
        {
            valid = true;
        }
        if (!valid)
        {
            cout << "  [Beep~~] valid option: type 0 ~ 9" << endl;
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
        }
    } while (!valid);

    switch (selection)
        {
        case 1:
            OMDlevelspecificDetectKeySize();
            break;

        case 2:
            levelspecificDetectKeySize();
            break;

        case 3:
            numcores = 1;
            OMD1p();
            break;

        case 4:
            numcores = 1;
            OMR2();
            break;

        case 5:
            numcores = 1;
            OMR3();
            break;
        
        case 6:
            numcores = 2;
            OMR2();
            break;

        case 7:
            numcores = 2;
            OMR3();
            break;
        
        case 8:
            numcores = 4;
            OMR2();
            break;

        case 9:
            numcores = 4;
            OMR3();
            break;

        case 0:
            return 0;
        }
    
    
}
