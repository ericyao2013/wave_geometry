/**
 * @file
 */

#ifndef WAVE_GEOMETRY_NUMERICALJACOBIAN_HPP
#define WAVE_GEOMETRY_NUMERICALJACOBIAN_HPP

namespace wave {
namespace internal {

/** Helper to get a vector from a vector expression *or* a scalar, both of which are
 * valid tangent types */
template <typename T, TICK_REQUIRES(!is_derived_expression<T>{})>
// Actually require a scalar
auto tangentValueAsVector(const T &t) -> Eigen::Matrix<T, 1, 1> {
    return Eigen::Matrix<T, 1, 1>{t};
}

template <typename T, TICK_REQUIRES(is_derived_expression<T>{})>
// Actually require a vector leaf
auto tangentValueAsVector(const ExpressionBase<T> &t) -> decltype(t.derived().value()) {
    return t.derived().value();
}

/** Helper to make a tangent type from a vector expression, if the tangent type might be a
 * scalar */
template <typename TangentType,
          typename Vector,
          TICK_REQUIRES(wave::internal::traits<TangentType>::Size != 1)>
auto makeTangent(const Vector &v) -> TangentType {
    return TangentType{v};
}

// This version is correct for scalar, but shouldn't actually be called unless we take
// derivative w.r.t. a scalar. It is just needed for the function to compile
template <typename TangentType,
          typename Vector,
          TICK_REQUIRES(wave::internal::traits<TangentType>::Size == 1)>
auto makeTangent(const Vector &v) -> TangentType {
    return TangentType{v[0]};
}

/** Evaluate an expression adding an offset along one dimension if expr === target
 * This helper requires the expression to already have been evaluated, yielding `value`
 */
template <typename Derived, typename TargetDerived, typename Scalar>
inline auto evaluateWithDeltaImpl(const Derived &expr,
                                  const TargetDerived &target,
                                  const plain_eval_t<Derived> &value,
                                  int coeff,
                                  Scalar delta) -> plain_eval_t<Derived> {
    static_assert(std::is_same<scalar_t<Derived>, Scalar>{}, "Scalar types must match");

    using TangentType = plain_tangent_t<Derived>;
    using EigenVector = Eigen::Matrix<Scalar, eval_traits<Derived>::TangentSize, 1>;
    if (isSame(expr, target)) {
        EigenVector delta_vec = EigenVector::Zero();
        delta_vec[coeff] = delta;
        // Add the offset. This + might be overloaded as box-plus!
        return plain_eval_t<Derived>{value + makeTangent<TangentType>(delta_vec)};
    } else {
        return value;
    }
}

/** Evaluates an an expression tree, adding a small offset in one dimension to the target
 *
 * Recurses down an existing Evaluator tree to save work.
 *
 * @note This implementation fully evaluates Eigen expressions at each step, in order for
 * the returned type to be the same whether or not expr === target. It is not as the
 * regular Evaluator, and is intended for testing only.
 *
 * Specialization for leaf expression
 */
template <typename Derived, typename TargetDerived, typename Enable = void>
struct EvaluatorWithDelta;

/** Specialization for leaf expression */
template <typename Derived, typename TargetDerived>
struct EvaluatorWithDelta<Derived, TargetDerived, enable_if_leaf_t<Derived>> {
    using Scalar = scalar_t<Derived>;
    using PlainType = plain_eval_t<Derived>;

    PlainType operator()(const Derived &expr,
                         const TargetDerived &target,
                         int coeff,
                         Scalar delta) const {
        const auto &value = evalImpl(get_expr_tag_t<Derived>{}, expr);
        return evaluateWithDeltaImpl(expr, target, PlainType{value}, coeff, delta);
    };
};

/** Specialization for unary expression */
template <typename Derived, typename TargetDerived>
struct EvaluatorWithDelta<Derived, TargetDerived, enable_if_unary_t<Derived>> {
    using Scalar = scalar_t<Derived>;
    using RhsEval = EvaluatorWithDelta<typename Derived::RhsDerived, TargetDerived>;
    using RhsValue = typename RhsEval::PlainType;
    using PlainExpr = typename traits<Derived>::template rebind<RhsValue>;
    using PlainType = plain_eval_t<PlainExpr>;

    PlainType operator()(const Derived &expr,
                         const TargetDerived &target,
                         int coeff,
                         Scalar delta) const {
        const auto &rhs_eval = RhsEval{};
        const auto &rhs_value = rhs_eval(expr.rhs(), target, coeff, delta);

        // Can't simply call evalImpl here - see comment in binary specialization
        // We copy part of what internal::prepareEvaluatorTo() does @todo simplify
        const auto &evaluable_expr = PrepareExpr<PlainExpr>::run(PlainExpr{rhs_value});
        using ExprType = tmp::remove_cr_t<decltype(evaluable_expr)>;
        const auto value = PlainType{Evaluator<ExprType>{evaluable_expr}()};

        return evaluateWithDeltaImpl(evaluable_expr, target, value, coeff, delta);
    }
};

/** Specialization for binary expression */
template <typename Derived, typename TargetDerived>
struct EvaluatorWithDelta<Derived, TargetDerived, enable_if_binary_t<Derived>> {
    using Scalar = scalar_t<Derived>;

    using LhsEval = EvaluatorWithDelta<typename Derived::LhsDerived, TargetDerived>;
    using RhsEval = EvaluatorWithDelta<typename Derived::RhsDerived, TargetDerived>;
    using LhsValue = typename LhsEval::PlainType;
    using RhsValue = typename RhsEval::PlainType;
    using PlainExpr = typename traits<Derived>::template rebind<LhsValue, RhsValue>;
    using PlainType = plain_eval_t<PlainExpr>;


    PlainType operator()(const Derived &expr,
                         const TargetDerived &target,
                         int coeff,
                         Scalar delta) const {
        const auto &lhs_eval = LhsEval{};
        const auto &rhs_eval = RhsEval{};
        const auto &lhs_value = lhs_eval(expr.lhs(), target, coeff, delta);
        const auto &rhs_value = rhs_eval(expr.rhs(), target, coeff, delta);

        // Can't simply call evalImpl here because it may not be evaluable. For example,
        // the original evaluator may have been for Compose(AngleAxisRotation, Identity)
        // which needs no conversion. Here by converting to PlainType we might ask for
        // Compose(AngleAxisRotation, MatrixRotation), which does need a conversion.
        // Therefore, fully evaluate the expression we have.
        const auto &evaluable_expr =
          PrepareExpr<PlainExpr>::run(PlainExpr{lhs_value, rhs_value});
        using ExprType = tmp::remove_cr_t<decltype(evaluable_expr)>;
        const auto value = PlainType{Evaluator<ExprType>{evaluable_expr}()};

        return evaluateWithDeltaImpl(evaluable_expr, target, value, coeff, delta);
    }
};


/** Numerically evaluate a jacobian of an expression tree, given an evaluator
 */
template <typename Derived, typename TargetDerived>
auto evaluateNumericalJacobianImpl(const Evaluator<Derived> &evaluator,
                                   const ExpressionBase<TargetDerived> &target)
  -> internal::jacobian_t<Derived, TargetDerived> {
    using Scalar = scalar_t<Derived>;
    auto jacobian = internal::jacobian_t<Derived, TargetDerived>{};

    const auto &expr = evaluator.expr;
    const auto &value = prepareOutput(evaluator);

    // @todo pick appropriate delta. Machine epsilon is not obviously appropriate since
    // for rotations, boxplus is not a linear addition. We also don't know the "x" value
    // here to pre-multiply. Also need to consider float vs double. This is super rough.
    const Scalar delta = std::sqrt(Eigen::NumTraits<Scalar>::epsilon());

    for (int i = 0; i < jacobian.cols(); ++i) {
        const auto forward_eval = internal::EvaluatorWithDelta<Derived, TargetDerived>{}(
          expr, target.derived(), i, delta);

        // Apply output functor (e.g. wrap in Framed)
        const auto forward_value = internal::prepareLeafForOutput<Derived>(forward_eval);

        // Calculate one column of Jacobian
        const auto &diff = tangent_t<Derived>{forward_value - value};
        jacobian.col(i) = tangentValueAsVector(diff) / delta;
    }
    return jacobian;
}

}  // namespace internal


/** Numerically evaluate a jacobian of an expression tree.
 *
 * @warning This function is intended only for testing analytic Jacobians. It may be
 * numerically imprecise and computationally slow.
 */
template <typename Derived, typename TargetDerived>
auto evaluateNumericalJacobian(const ExpressionBase<Derived> &expr,
                               const ExpressionBase<TargetDerived> &target)
  -> internal::jacobian_t<Derived, TargetDerived> {
    using OutputType = internal::plain_output_t<Derived>;
    const auto &v_eval = internal::prepareEvaluatorTo<OutputType>(expr.derived());

    return internal::evaluateNumericalJacobianImpl(v_eval, target.derived());
}

/** Numerically evaluate multiple Jacobians of am expression tree.
 *
 * @returns a tuple with the result of calling evaluateNumericalJacobian() for each of
 * Targets.
 *
 * @warning This function is intended only for testing analytic Jacobians. It may be
 * numerically imprecise and computationally slow.
 */
template <typename Derived, typename... Targets>
auto evaluateNumericalJacobians(const ExpressionBase<Derived> &expr,
                                const ExpressionBase<Targets> &... targets)
  -> std::tuple<internal::jacobian_t<Derived, Targets>...> {
    // Get the correct evaluator
    using OutputType = internal::plain_output_t<Derived>;
    const auto &v_eval = internal::prepareEvaluatorTo<OutputType>(expr.derived());

    return std::make_tuple(
      internal::evaluateNumericalJacobianImpl(v_eval, targets.derived())...);
}


}  // namespace wave

#endif  // WAVE_GEOMETRY_NUMERICALJACOBIAN_HPP
